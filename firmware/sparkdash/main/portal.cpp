#include "app.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <cstring>
#include <string>
namespace app {
static httpd_handle_t server;
static char token[33];
static std::string quote(const char *s) {
    std::string r = "\"";
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') {
            r += '\\';
            r += c;
        } else if (c < 32) {
            char b[7];
            snprintf(b, sizeof b, "\\u%04x", c);
            r += b;
        } else
            r += c;
    }
    return r + '"';
}
static bool ap_only(httpd_req_t *r) {
    sockaddr_in local{};
    socklen_t len = sizeof local;
    if (getsockname(httpd_req_to_sockfd(r), reinterpret_cast<sockaddr *>(&local), &len) ||
        local.sin_addr.s_addr != inet_addr("192.168.4.1")) {
        httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
        return false;
    }
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    return true;
}
static esp_err_t json(httpd_req_t *r, const std::string &s) {
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, s.data(), s.size());
}
static const char Page[] =
    R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>sparkDash setup</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#151615;color:#f3f2ee}body{max-width:420px;margin:32px auto;padding:0 24px}h1{font-size:28px;font-weight:600;color:#e8ac2b}p{color:#bbb;line-height:1.5}label{display:block;margin:20px 0 6px}input,select,button{box-sizing:border-box;width:100%;font:inherit;font-size:16px;padding:12px;border:1px solid #555;border-radius:8px;background:#242624;color:#fff}button{margin-top:20px;background:#e8ac2b;color:#191919;font-weight:600;cursor:pointer}#scan{background:#242624;color:#eee;margin-top:8px}#status{min-height:48px;white-space:pre-wrap}small{color:#aaa}</style><h1>sparkDash setup</h1><p>Connect your display to a 2.4 GHz Wi-Fi network and your sparkDash server.</p><form id="form"><label for="networks">Nearby networks</label><select id="networks"><option value="">Enter a network below</option></select><button id="scan" type="button">Scan networks</button><label for="ssid">Network name</label><input id="ssid" name="ssid" maxlength="32" autocomplete="off" required><label for="password">Wi-Fi password</label><input id="password" name="password" type="password" minlength="8" maxlength="63" autocomplete="new-password" required><label for="url">sparkDash server</label><input id="url" name="url" value="http://dgx01.local:5555" maxlength="319" type="url" required><small>HTTP, hostname or IP address, and optional port.</small><button id="save">Save and connect</button></form><p id="status" role="status" aria-live="polite">Ready</p><script>
const token=)HTML";
static const char Script[] = R"HTML(;
const form=document.getElementById('form'),statusEl=document.getElementById('status'),save=document.getElementById('save'),networks=document.getElementById('networks');
networks.onchange=()=>{if(networks.value)document.getElementById('ssid').value=networks.value};
document.getElementById('scan').onclick=async()=>{statusEl.textContent='Scanning…';try{for(let i=0;i<20;i++){const r=await fetch('/setup/scan');const d=await r.json();if(!d.scanning){networks.replaceChildren(new Option('Enter a network below',''));for(const n of d.networks)networks.add(new Option(n.ssid+' ('+n.rssi+' dBm)',n.ssid));statusEl.textContent='Scan complete';return}await new Promise(r=>setTimeout(r,500))}}catch{statusEl.textContent='Scan failed. Enter the network name manually.'}};
form.onsubmit=async e=>{e.preventDefault();save.disabled=true;const data=new URLSearchParams(new FormData(form));data.set('token',token);try{const r=await fetch('/setup/config',{method:'POST',body:data});statusEl.textContent=await r.text();if(!r.ok)save.disabled=false;}catch{statusEl.textContent='Connection lost. Check the display.';save.disabled=false}};
setInterval(async()=>{try{const r=await fetch('/setup/status');const d=await r.json();statusEl.textContent=d.status;if(d.status.includes('failed')||d.status.includes('Could not'))save.disabled=false;}catch{}},1500);
</script></html>)HTML";
static esp_err_t page(httpd_req_t *r) {
    if (!ap_only(r))
        return ESP_OK;
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_send_chunk(r, Page, strlen(Page));
    auto t = quote(token);
    httpd_resp_send_chunk(r, t.data(), t.size());
    httpd_resp_send_chunk(r, Script, strlen(Script));
    return httpd_resp_send_chunk(r, nullptr, 0);
}
static esp_err_t get_status(httpd_req_t *r) {
    if (!ap_only(r))
        return ESP_OK;
    View v;
    snapshot(v);
    return json(r, "{\"status\":" + quote(v.status) + "}");
}
static bool scanning = false;
static esp_err_t scan(httpd_req_t *r) {
    if (!ap_only(r))
        return ESP_OK;
    if (!scanning) {
        if (!send(CommandType::Scan))
            return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Try again");
        scanning = true;
        return json(r, "{\"scanning\":true}");
    }
    if (!scan_done)
        return json(r, "{\"scanning\":true}");
    scanning = false;
    uint16_t count = 16;
    wifi_ap_record_t results[16]{};
    if (esp_wifi_scan_get_ap_records(&count, results) != ESP_OK)
        count = 0;
    std::string s = "{\"scanning\":false,\"networks\":[";
    bool first = true;
    for (unsigned i = 0; i < count; i++) {
        if (results[i].authmode == WIFI_AUTH_OPEN ||
            results[i].authmode == WIFI_AUTH_WPA2_ENTERPRISE)
            continue;
        if (!first)
            s += ',';
        first = false;
        char n[24];
        snprintf(n, sizeof n, "%d", results[i].rssi);
        s += "{\"ssid\":" + quote(reinterpret_cast<char *>(results[i].ssid)) + ",\"rssi\":" + n +
             "}";
    }
    return json(r, s + "]}");
}
static esp_err_t configure(httpd_req_t *r) {
    if (!ap_only(r))
        return ESP_OK;
    if (r->content_len <= 0 || r->content_len > 2048)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Configuration must be at most 2 KiB");
    char body[2049]{};
    size_t used = 0;
    while (used < size_t(r->content_len)) {
        int n = httpd_req_recv(r, body + used, r->content_len - used);
        if (n <= 0)
            return httpd_resp_send_err(r, HTTPD_408_REQ_TIMEOUT, "Incomplete configuration");
        used += n;
    }
    char supplied[40];
    if (!spark::decode_form(body, "token", supplied, sizeof supplied) || strcmp(supplied, token))
        return httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "Reload setup page");
    Command c{};
    c.type = CommandType::Configure;
    char err[128];
    bool valid =
        spark::decode_form(body, "ssid", c.connection.ssid, sizeof c.connection.ssid) &&
        spark::decode_form(body, "password", c.connection.password, sizeof c.connection.password) &&
        spark::decode_form(body, "url", c.connection.url, sizeof c.connection.url);
    memset(body, 0, sizeof body);
    if (!valid)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Missing or oversized field");
    if (!spark::validate_connection(c.connection, err, sizeof err))
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, err);
    if (xQueueSend(commands, &c, 0) != pdTRUE)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Try again");
    memset(c.connection.password, 0, sizeof c.connection.password);
    return httpd_resp_sendstr(r, "Connecting. Watch the display for progress.");
}
void portal_start() {
    if (server)
        return;
    for (int i = 0; i < 4; i++)
        snprintf(token + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
    scanning = false;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = nullptr},
        {.uri = "/setup/status", .method = HTTP_GET, .handler = get_status, .user_ctx = nullptr},
        {.uri = "/setup/scan", .method = HTTP_GET, .handler = scan, .user_ctx = nullptr},
        {.uri = "/setup/config", .method = HTTP_POST, .handler = configure, .user_ctx = nullptr}};
    for (auto &r : routes)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &r));
}
void portal_stop() {
    if (server) {
        httpd_stop(server);
        server = nullptr;
    }
    memset(token, 0, sizeof token);
}
} // namespace app
