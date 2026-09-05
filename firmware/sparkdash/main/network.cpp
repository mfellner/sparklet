#include "app.hpp"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace app {
std::mutex mutex;
spark::Cache cache;
View state;
QueueHandle_t commands;
std::atomic<bool> wifi_connected{false}, scan_done{true};
std::atomic<uint32_t> network_stack_free{0}, ui_stack_free{0}, ui_brightness{0}, ui_dimmed{0};
static spark::Connection saved, candidate;
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
static char test_url[320]{};
#endif
static bool has_saved = false, testing = false, setup_mode = false, nvs_ready = false;
static uint64_t test_deadline = 0, ap_stop_at = 0, reconnect_at = 0;
static unsigned wifi_failures = 0;
static spark::Scheduler schedule;
static esp_netif_t *sta_netif;
static char body[spark::BodyLimit + 1];
static char resolved[20]{}, resolved_host[128]{};
static bool previous_connected = false;
static std::mutex request_mutex;
static esp_http_client_handle_t active_client = nullptr;
static esp_timer_handle_t request_deadline;
static uint64_t active_deadline_ms = 0;
static void cancel_request(void *) {
    std::lock_guard<std::mutex> guard(request_mutex);
    if (active_client && now_ms() >= active_deadline_ms)
        esp_http_client_cancel_request(active_client);
}
uint64_t now_ms() {
    return esp_timer_get_time() / 1000;
}
void status(const char *s) {
    std::lock_guard<std::mutex> lock(mutex);
    spark::copy_text(state.status, sizeof state.status, s);
    state.revision++;
}
void snapshot(View &out) {
    std::lock_guard<std::mutex> lock(mutex);
    out = state;
    out.count = cache.count;
    out.selected = cache.selected;
    out.overflow = cache.overflow;
    if (cache.count)
        out.node = cache.nodes[cache.selected];
    else
        out.node = spark::Node{};
}
bool send(CommandType t) {
    // Browsing cached data must never wait behind a blocking network request.
    if (t == CommandType::Previous || t == CommandType::Next) {
        std::lock_guard<std::mutex> guard(mutex);
        spark::move(cache, t == CommandType::Previous ? -1 : 1);
        state.revision++;
        t = CommandType::SelectionChanged;
    }
    Command c{};
    c.type = t;
    return xQueueSend(commands, &c, 0) == pdTRUE;
}
static bool save_blob(const char *key, const void *p, size_t n) {
    if (!nvs_ready)
        return false;
    nvs_handle_t h;
    auto e = nvs_open("sparkdash", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return false;
    e = nvs_set_blob(h, key, p, n);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}
static esp_err_t load_blob(const char *key, void *p, size_t n) {
    nvs_handle_t h;
    auto e = nvs_open("sparkdash", NVS_READONLY, &h);
    if (e != ESP_OK)
        return e;
    size_t size = n;
    e = nvs_get_blob(h, key, p, &size);
    nvs_close(h);
    return e == ESP_OK && size != n ? ESP_ERR_INVALID_SIZE : e;
}
static void events(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        wifi_connected = true;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        wifi_connected = false;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED)
        ESP_LOGI("setup", "Phone associated with setup AP");
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(data);
        ESP_LOGW("setup", "Setup client disconnected: reason=%u", unsigned(event->reason));
    }
    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED)
        ESP_LOGI("setup", "Setup client received DHCP address");
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE)
        scan_done = true;
}
static void join(const spark::Connection &c) {
    wifi_connected = false;
    esp_wifi_disconnect();
    wifi_config_t wc{};
    memcpy(wc.sta.ssid, c.ssid, strlen(c.ssid));
    memcpy(wc.sta.password, c.password, strlen(c.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    esp_wifi_connect();
    reconnect_at = now_ms() + 5000;
    status("Joining Wi-Fi");
}
static void enter_setup() {
    testing = false;
    ap_stop_at = 0;
    setup_mode = true;
    resolved[0] = 0;
    esp_wifi_disconnect();
    wifi_connected = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char name[33], pass[17];
    snprintf(name, sizeof name, "SparkDash-%02X%02X", mac[4], mac[5]);
    const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 12; i++)
        pass[i] = chars[esp_random() % (sizeof chars - 1)];
    pass[12] = 0;
    wifi_config_t ap{};
    spark::copy_text(reinterpret_cast<char *>(ap.ap.ssid), sizeof ap.ap.ssid, name);
    ap.ap.ssid_len = strlen(name);
    spark::copy_text(reinterpret_cast<char *>(ap.ap.password), sizeof ap.ap.password, pass);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 2;
    ap.ap.channel = 1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.setup = true;
        spark::copy_text(state.ap_ssid, sizeof state.ap_ssid, name);
        spark::copy_text(state.ap_password, sizeof state.ap_password, pass);
        state.revision++;
    }
    portal_start();
    status("Connect phone to the setup Wi-Fi");
}
static void leave_setup() {
    portal_stop();
    setup_mode = false;
    testing = false;
    ap_stop_at = 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.setup = false;
        memset(state.ap_password, 0, sizeof state.ap_password);
        state.revision++;
    }
    schedule.reconnect();
    resolved[0] = 0;
}
struct Transfer {
    size_t used = 0;
    uint64_t deadline;
    bool oversized = false, timed_out = false;
    int retry_after = 2;
};
static esp_err_t http_event(esp_http_client_event_t *ev) {
    auto &t = *static_cast<Transfer *>(ev->user_data);
    if (now_ms() > t.deadline) {
        t.timed_out = true;
        return ESP_FAIL;
    }
    if (ev->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(ev->header_key, "Retry-After"))
        t.retry_after = std::clamp(atoi(ev->header_value), 2, 60);
    if (ev->event_id == HTTP_EVENT_ON_DATA) {
        if (ev->data_len < 0 || size_t(ev->data_len) > spark::BodyLimit - t.used) {
            t.oversized = true;
            return ESP_FAIL;
        }
        memcpy(body + t.used, ev->data, ev->data_len);
        t.used += ev->data_len;
    }
    return ESP_OK;
}
struct HttpResult {
    bool transport = false;
    int code = 0;
    size_t length = 0;
    int retry_after = 2;
    char error[80]{};
};
static bool resolve(const spark::Url &u) {
    if (*resolved && !strcmp(resolved_host, u.host))
        return true;
    status("Resolving server");
    size_t len = strlen(u.host);
    esp_ip4_addr_t ip{};
    if (len > 6 && !strcmp(u.host + len - 6, ".local")) {
        char shortname[128];
        spark::copy_text(shortname, sizeof shortname, u.host);
        shortname[len - 6] = 0;
        if (mdns_query_a(shortname, 2000, &ip) != ESP_OK)
            return false;
        snprintf(resolved, sizeof resolved, IPSTR, IP2STR(&ip));
    } else {
        addrinfo hints{}, *r = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(u.host, nullptr, &hints, &r) || !r)
            return false;
        auto *address = reinterpret_cast<sockaddr_in *>(r->ai_addr);
        inet_ntop(AF_INET, &address->sin_addr, resolved, sizeof resolved);
        freeaddrinfo(r);
    }
    spark::copy_text(resolved_host, sizeof resolved_host, u.host);
    return true;
}
static HttpResult fetch(const spark::Url &u, const char *path) {
    HttpResult result;
    if (!resolve(u)) {
        spark::copy_text(result.error, sizeof result.error, "Server name lookup failed");
        return result;
    }
    char url[600], host[160];
    snprintf(url, sizeof url, "http://%s:%u%s%s", resolved, u.port, u.prefix, path);
    snprintf(host, sizeof host, "%s:%u", u.host, u.port);
    Transfer t{};
    t.deadline = now_ms() + 5000;
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.timeout_ms = 3000;
    cfg.event_handler = http_event;
    cfg.user_data = &t;
    cfg.disable_auto_redirect = true;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 768;
    auto *client = esp_http_client_init(&cfg);
    if (!client) {
        spark::copy_text(result.error, sizeof result.error, "HTTP allocation failed");
        return result;
    }
    esp_http_client_set_header(client, "Host", host);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    {
        std::lock_guard<std::mutex> guard(request_mutex);
        active_client = client;
        active_deadline_ms = t.deadline;
    }
    ESP_ERROR_CHECK(esp_timer_start_once(request_deadline, 5000000));
    auto err = esp_http_client_perform(client);
    {
        std::lock_guard<std::mutex> guard(request_mutex);
        active_client = nullptr;
    }
    esp_timer_stop(request_deadline);
    result.code = esp_http_client_get_status_code(client);
    result.length = t.used;
    result.retry_after = t.retry_after;
    body[t.used] = 0;
    result.transport = err == ESP_OK && !t.oversized && !t.timed_out && now_ms() <= t.deadline;
    if (!result.transport)
        spark::copy_text(result.error, sizeof result.error,
                         t.oversized                            ? "Response exceeds 16 KiB"
                         : t.timed_out || now_ms() > t.deadline ? "Server response timed out"
                                                                : "Server unreachable");
    esp_http_client_cleanup(client);
    return result;
}
static void update_link() {
    bool connected = wifi_connected.load();
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.connected = connected;
        if (connected) {
            esp_netif_ip_info_t info{};
            esp_netif_get_ip_info(sta_netif, &info);
            snprintf(state.ip, sizeof state.ip, IPSTR, IP2STR(&info.ip));
            wifi_ap_record_t ap{};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                state.rssi = ap.rssi;
                spark::copy_text(state.ssid, sizeof state.ssid, reinterpret_cast<char *>(ap.ssid));
            }
        }
    }
    if (connected && !previous_connected) {
        wifi_failures = 0;
        schedule.reconnect();
        resolved[0] = 0;
        status(setup_mode ? "Wi-Fi connected" : "Loading node list");
    }
    previous_connected = connected;
}
static void worker(void *) {
    static spark::Cache incoming;
    uint64_t diag_at = 0;
    for (;;) {
        Command command;
        while (xQueueReceive(commands, &command, 0) == pdTRUE) {
            switch (command.type) {
            case CommandType::Previous:
            case CommandType::Next: {
                std::lock_guard<std::mutex> lock(mutex);
                spark::move(cache, command.type == CommandType::Previous ? -1 : 1);
                schedule.select();
                state.revision++;
                break;
            }
            case CommandType::SelectionChanged:
                schedule.select();
                break;
            case CommandType::Setup:
                if (!setup_mode)
                    enter_setup();
                break;
            case CommandType::CancelSetup:
                if (setup_mode && has_saved) {
                    leave_setup();
                    join(saved);
                }
                break;
            case CommandType::Forget: {
                bool forgotten = false;
                if (!nvs_ready) {
                    // Reached only after the explicit on-device Forget confirmation.
                    nvs_ready =
                        nvs_flash_erase_partition("nvs") == ESP_OK && nvs_flash_init() == ESP_OK;
                }
                if (nvs_ready) {
                    nvs_handle_t h;
                    if (nvs_open("sparkdash", NVS_READWRITE, &h) == ESP_OK) {
                        auto erased = nvs_erase_key(h, "connection");
                        forgotten = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                                    nvs_commit(h) == ESP_OK;
                        nvs_close(h);
                    }
                }
                if (!forgotten) {
                    status("Could not complete connection reset; retry from Settings");
                    break;
                }
                has_saved = false;
                saved = spark::Connection{};
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    cache.count = 0;
                    state.config_error = !nvs_ready;
                }
                if (setup_mode)
                    portal_stop();
                enter_setup();
                break;
            }
            case CommandType::SavePreferences: {
                auto p = command.preferences;
                p.brightness = std::clamp<unsigned>(p.brightness, 10, 100);
                p.dim_seconds = std::clamp<unsigned>(p.dim_seconds, 30, 600);
                if (save_blob("preferences", &p, sizeof p)) {
                    std::lock_guard<std::mutex> lock(mutex);
                    state.preferences = p;
                    state.revision++;
                } else
                    status("Could not save display preferences");
                break;
            }
            case CommandType::Configure: {
                char error[128];
                if (setup_mode && !testing &&
                    spark::validate_connection(command.connection, error, sizeof error)) {
                    candidate = command.connection;
                    testing = true;
                    test_deadline = now_ms() + 30000;
                    join(candidate);
                } else if (setup_mode && !testing)
                    status(error);
                break;
            }
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
            case CommandType::TestUrl: {
                if (setup_mode)
                    break;
                spark::Url parsed;
                char error[128];
                if (*command.connection.url &&
                    !spark::parse_url(command.connection.url, parsed, error, sizeof error))
                    break;
                spark::copy_text(test_url, sizeof test_url, command.connection.url);
                resolved[0] = 0;
                schedule.reconnect();
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    cache = spark::Cache{};
                    state.listed = false;
                    spark::copy_text(state.url, sizeof state.url, *test_url ? test_url : saved.url);
                }
                status(*test_url ? "USB test server; saved connection unchanged"
                                 : "Loading node list");
                break;
            }
            case CommandType::TestReconnect:
                if (!setup_mode) {
                    esp_wifi_disconnect();
                    wifi_connected = false;
                    reconnect_at = now_ms() + 1000;
                }
                break;
#endif
            case CommandType::Scan:
                if (setup_mode && !testing && scan_done) {
                    scan_done = false;
                    wifi_scan_config_t sc{};
                    sc.show_hidden = true;
                    if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
                        scan_done = true;
                        status("Wi-Fi scan unavailable; enter SSID manually");
                    }
                }
                break;
            }
        }
        update_link();
        uint64_t now = now_ms();
        if (setup_mode) {
            if (testing && wifi_connected) {
                if (save_blob("connection", &candidate, sizeof candidate)) {
                    saved = candidate;
                    has_saved = true;
                    testing = false;
                    ap_stop_at = now + 8000;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        cache.count = 0;
                        state.listed = false;
                        spark::copy_text(state.url, sizeof state.url, saved.url);
                    }
                    status("Saved. Connected; closing setup in 8 seconds");
                } else {
                    testing = false;
                    status("Could not save configuration; previous settings retained");
                }
            } else if (testing && now >= test_deadline) {
                testing = false;
                esp_wifi_disconnect();
                status("Wi-Fi connection failed. Check password and retry");
            }
            if (ap_stop_at && now >= ap_stop_at)
                leave_setup();
        } else if (!wifi_connected) {
            if (has_saved && now >= reconnect_at) {
                esp_wifi_connect();
                unsigned sec = std::min(30u, 2u << std::min(wifi_failures++, 4u));
                reconnect_at = now + sec * 1000;
                status("Wi-Fi disconnected; reconnecting");
            }
        } else if (has_saved) {
            size_t count, selected;
            {
                std::lock_guard<std::mutex> lock(mutex);
                count = cache.count;
                selected = cache.selected;
            }
            auto work = schedule.next(now, count, selected);
            if (work.kind != spark::Scheduler::Kind::None) {
                spark::Url url;
                char err[128]{}, id[65]{}, path[256];
                const char *request_url = saved.url;
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
                if (*test_url)
                    request_url = test_url;
#endif
                bool valid = spark::parse_url(request_url, url, err, sizeof err);
                if (work.kind == spark::Scheduler::Kind::List)
                    spark::copy_text(path, sizeof path, "/api/sparks");
                else {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        spark::copy_text(id, sizeof id, cache.nodes[work.index].id);
                    }
                    char encoded[193];
                    spark::percent_encode(id, encoded, sizeof encoded);
                    snprintf(path, sizeof path, "/api/sparks/%s/metrics", encoded);
                }
                auto response = valid ? fetch(url, path) : HttpResult{};
                bool ok = false;
                if (response.transport && response.code == 200) {
                    if (work.kind == spark::Scheduler::Kind::List) {
                        ok = spark::parse_list(body, response.length, incoming, err, sizeof err);
                        if (ok) {
                            std::lock_guard<std::mutex> lock(mutex);
                            spark::reconcile(cache, incoming);
                            state.listed = true;
                            state.revision++;
                        }
                    } else {
                        spark::Node record;
                        ok = spark::parse_node(body, response.length, id, record, err, sizeof err);
                        if (ok) {
                            record.received = true;
                            record.received_ms = now_ms();
                            std::lock_guard<std::mutex> lock(mutex);
                            for (size_t i = 0; i < cache.count; i++)
                                if (!strcmp(cache.nodes[i].id, id))
                                    cache.nodes[i] = record;
                            state.revision++;
                        }
                    }
                } else if (!response.transport)
                    spark::copy_text(err, sizeof err, response.error);
                else if (response.code == 401 || response.code == 403)
                    spark::copy_text(err, sizeof err, "Server access denied");
                else if (response.code == 429)
                    spark::copy_text(err, sizeof err, "Server rate limited");
                else
                    snprintf(err, sizeof err, "Server HTTP %d", response.code);
                schedule.completed(work, now_ms(), count);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    state.requests++;
                    if (!ok) {
                        state.errors++;
                        if (*id)
                            for (size_t i = 0; i < cache.count; i++)
                                if (!strcmp(cache.nodes[i].id, id))
                                    spark::copy_text(cache.nodes[i].error,
                                                     sizeof cache.nodes[i].error, err);
                    }
                }
                if (ok) {
                    schedule.success();
                    status("Connected");
                } else {
                    schedule.failed(now_ms(), esp_random());
                    if (work.kind == spark::Scheduler::Kind::List)
                        schedule.list_required = true;
                    if (schedule.failures >= 3)
                        resolved[0] = 0;
                    if (response.code == 429)
                        schedule.retry_due = now_ms() + response.retry_after * 1000;
                    if (response.code == 404 && *id)
                        schedule.list_required = true;
                    status(*err ? err : "Incompatible response");
                }
            }
        }
        if (now >= diag_at) {
            diag_at = now + 60000;
            ESP_LOGI("health",
                     "uptime_ms=%llu heap=%u minimum=%u largest=%u net_stack=%u requests=%u "
                     "errors=%u nodes=%u",
                     (unsigned long long)now,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(nullptr), state.requests, state.errors,
                     (unsigned)cache.count);
        }
        network_stack_free = uxTaskGetStackHighWaterMark(nullptr);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void network_start() {
    esp_timer_create_args_t deadline_config{};
    deadline_config.callback = cancel_request;
    deadline_config.name = "http_deadline";
    ESP_ERROR_CHECK(esp_timer_create(&deadline_config, &request_deadline));
    commands = xQueueCreate(8, sizeof(Command));
    assert(commands);
    auto nerr = nvs_flash_init();
    nvs_ready = nerr == ESP_OK;
    if (nvs_ready) {
        auto err = load_blob("connection", &saved, sizeof saved);
        char error[128];
        has_saved = err == ESP_OK && spark::validate_connection(saved, error, sizeof error);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            state.config_error = true;
        if (err == ESP_OK && !has_saved)
            state.config_error = true;
        spark::Preferences p;
        if (load_blob("preferences", &p, sizeof p) == ESP_OK && p.version == 1 &&
            p.brightness >= 10 && p.brightness <= 100 && p.dim_seconds >= 30 &&
            p.dim_seconds <= 600)
            state.preferences = p;
    } else
        state.config_error = true;
    // Keep invalid on-flash data available for explicit recovery, but never consume
    // its potentially unterminated strings for UI or networking.
    if (!has_saved)
        saved = spark::Connection{};
    spark::copy_text(state.url, sizeof state.url, saved.url);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, events, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, events, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set("sparkdash-display");
    if (has_saved)
        join(saved);
    else
        enter_setup();
    BaseType_t ok = xTaskCreate(worker, "spark_network", 8192, nullptr, 4, nullptr);
    assert(ok == pdPASS);
}
} // namespace app
