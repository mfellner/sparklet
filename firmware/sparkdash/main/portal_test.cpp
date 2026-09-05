#include "app.hpp"
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include <cstring>

namespace app {
namespace {
// Reuse the idle metrics buffer while setup suspends polling.
char *response;
constexpr size_t ResponseSize = 8192;
size_t response_used;
bool overflow, scratch_safe;
spark::Connection before, after;
char *form;
constexpr size_t FormSize = 2050;

esp_err_t received(esp_http_client_event_t *e) {
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (e->data_len < 0 || size_t(e->data_len) >= ResponseSize - response_used) {
            overflow = true;
            return ESP_FAIL;
        }
        memcpy(response + response_used, e->data, e->data_len);
        response_used += e->data_len;
        response[response_used] = 0;
    }
    return ESP_OK;
}
int request(const char *path, const char *body = nullptr) {
    char url[96];
    snprintf(url, sizeof url, "http://192.168.4.1%s", path);
    esp_http_client_config_t c{};
    c.url = url;
    c.timeout_ms = 3000;
    c.event_handler = received;
    c.buffer_size = 512;
    c.buffer_size_tx = 512;
    auto client = esp_http_client_init(&c);
    if (!client)
        return -1;
    response_used = 0;
    response[0] = 0;
    overflow = false;
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    auto result = esp_http_client_perform(client);
    int code = result == ESP_OK && !overflow ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    return code;
}
bool connection_blob(spark::Connection &out) {
    nvs_handle_t h;
    if (nvs_open("sparkdash", NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t size = sizeof out;
    bool ok = nvs_get_blob(h, "connection", &out, &size) == ESP_OK && size == sizeof out;
    nvs_close(h);
    return ok;
}
bool check(const char *name, bool ok) {
    ESP_LOGI("qa_portal", "check=%s pass=%u heap=%u largest=%u portal_stack=%u diag_stack=%u", name,
             unsigned(ok), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             unsigned(portal_stack_free.load()), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    return ok;
}
bool wait_state(bool setup, bool connected, uint32_t seconds, const char *status = nullptr) {
    const uint64_t until = now_ms() + seconds * 1000;
    do {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state.setup == setup && state.connected == connected &&
                (!status || strstr(state.status, status)))
                return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (now_ms() < until);
    return false;
}
bool set_test_url(const char *url) {
    static Command command;
    command = Command{};
    command.type = CommandType::TestUrl;
    spark::copy_text(command.connection.url, sizeof command.connection.url, url);
    if (xQueueSend(commands, &command, 0) != pdTRUE)
        return false;
    const char *expected = *url ? url : before.url;
    uint64_t deadline = now_ms() + 10000;
    do {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!strcmp(state.url, expected))
                return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (now_ms() < deadline);
    return false;
}
bool candidate_form(const char *token, const char *password) {
    snprintf(form, FormSize, "token=%s&ssid=", token);
    size_t n = strlen(form);
    if (!spark::percent_encode(before.ssid, form + n, FormSize - n))
        return false;
    n = strlen(form);
    snprintf(form + n, FormSize - n, "&password=");
    n = strlen(form);
    if (!spark::percent_encode(password, form + n, FormSize - n))
        return false;
    n = strlen(form);
    snprintf(form + n, FormSize - n, "&url=");
    n = strlen(form);
    return spark::percent_encode(before.url, form + n, FormSize - n);
}
bool exercise() {
    if (!check("saved_baseline", connection_blob(before)))
        return false;
    if (!check("volatile_unavailable_server", set_test_url("http://192.0.2.1:5555")))
        return false;
    if (!send(CommandType::Setup) || !wait_state(true, false, 8))
        return check("setup_started", false);
    scratch_safe = true;
    int code = -1;
    for (int i = 0; i < 10 && code != 200; ++i) {
        vTaskDelay(pdMS_TO_TICKS(200));
        code = request("/");
    }
    char token[33]{};
    const char *start = strstr(response, "const token=\"");
    if (!check("page", code == 200 && start && strlen(start + 13) >= 33))
        return false;
    memcpy(token, start + 13, 32);
    if (!check("status", request("/setup/status") == 200 && !strstr(response, before.password)))
        return false;
    if (!check("token_required", request("/setup/config", "ssid=test") == 403))
        return false;
    snprintf(form, FormSize, "token=%s", token);
    if (!check("missing_fields", request("/setup/config", form) == 400))
        return false;
    snprintf(form, FormSize,
             "token=%s&ssid=synthetic&password=test-only-pass&url=https://example.invalid", token);
    if (!check("https_rejected", request("/setup/config", form) == 400))
        return false;
    // Exactly 2048 bytes reaches field validation; >2048 is rejected before reception.
    memset(form, 'x', 2048);
    form[2048] = 0;
    if (!check("max_submission", request("/setup/config", form) == 403))
        return false;
    form[2048] = 'x';
    form[2049] = 0;
    if (!check("oversized_submission", request("/setup/config", form) == 400))
        return false;
    bool scan_complete = false;
    for (unsigned i = 0; i < 30; ++i) {
        if (request("/setup/scan") != 200)
            return check("scan_http", false);
        if (strstr(response, "\"scanning\":false")) {
            scan_complete = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!check("scan", scan_complete))
        return false;
    // Random nonexistent test SSID; no production router/service is changed.
    snprintf(form, FormSize,
             "token=%s&ssid=Sparklet-QA-%08lx&password=test-only-pass&url=http://192.0.2.1:5555",
             token, (unsigned long)esp_random());
    if (!check("candidate_accepted", request("/setup/config", form) == 200))
        return false;
    if (!check("failed_candidate", wait_state(true, false, 35, "connection failed")))
        return false;
    if (!check("saved_record_unchanged",
               connection_blob(after) && !memcmp(&before, &after, sizeof before)))
        return false;
    char wrong[33];
    snprintf(wrong, sizeof wrong, "qa-%08lx-%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    if (!strcmp(wrong, before.password))
        wrong[0] = wrong[0] == 'q' ? 'x' : 'q';
    if (!candidate_form(token, wrong) ||
        !check("wrong_password_accepted_for_test", request("/setup/config", form) == 200))
        return false;
    // Wait for the new attempt to leave the preceding failure status first.
    if (!wait_state(true, false, 5, "Joining") ||
        !check("wrong_password_failed", wait_state(true, false, 35, "connection failed")))
        return false;
    if (!check("wrong_password_retained_record",
               connection_blob(after) && !memcmp(&before, &after, sizeof before)))
        return false;
    if (!candidate_form(token, before.password) ||
        !check("valid_candidate", request("/setup/config", form) == 200))
        return false;
    if (!check("wifi_saved_without_server", wait_state(false, true, 40)))
        return false;
    return check("identical_saved_configuration",
                 connection_blob(after) && !memcmp(&before, &after, sizeof before));
}
} // namespace
void portal_self_test() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state.setup || !state.connected) {
            check("requires_saved_live_connection", false);
            return;
        }
    }
    ESP_LOGI("qa_portal", "begin=1");
    response = portal_test_buffer();
    form = response + ResponseSize;
    scratch_safe = false;
    bool ok = exercise();
    // Cleanup runs on every test result and does not overwrite credentials.
    // Release borrowed scratch before the worker can resume metrics.
    if (scratch_safe)
        memset(response, 0, spark::BodyLimit + 1);
    response = form = nullptr;
    portal_test_buffer_release();
    send(CommandType::CancelSetup);
    bool restored = wait_state(false, true, 45);
    bool source_restored = set_test_url("");
    bool same = connection_blob(after) && !memcmp(&before, &after, sizeof before);
    check("restored_connection", restored && same && source_restored);
    before = spark::Connection{};
    after = spark::Connection{};
    ESP_LOGI("qa_portal", "complete=1 pass=%u",
             unsigned(ok && restored && same && source_restored));
}
} // namespace app
#endif
