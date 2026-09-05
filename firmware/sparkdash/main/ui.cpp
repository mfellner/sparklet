#include "app.hpp"
#include "board.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace app {
namespace {
enum class Page { Overview, Details, Settings, Setup, SetupLink, Forget };
Page page = Page::Overview, built = Page::Forget;
View view;
lv_obj_t *title, *subtitle, *status_label, *role, *position, *model, *throughput, *power,
    *available, *message, *details, *brightness_slider, *dim_slider, *prefs_label;
lv_obj_t *values[3], *bars[3], *setup_qr;
char qr_payload[160]{};
bool setup_page() {
    return page == Page::Setup || page == Page::SetupLink;
}
uint64_t last_touch = 0;
bool dimmed = false, was_down = false, consume_touch = false;
int start_x = 0, start_y = 0;
unsigned last_brightness = 0;
constexpr uint32_t Bg = 0x151615, Text = 0xf1f1ed, Muted = 0xaaa9a4, Amber = 0xe8ac2b,
                   Red = 0xf05b51, Green = 0x4ac09a;
static lv_color_t color(uint32_t x) {
    return lv_color_hex(x);
}
void set(lv_obj_t *o, const char *s) {
    if (o && strcmp(lv_label_get_text(o), s))
        lv_label_set_text(o, s);
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, uint32_t c = Text) {
    auto *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color(c), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}
void on_action(lv_event_t *e) {
    auto action = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    switch (action) {
    case 1:
        send(CommandType::Previous);
        break;
    case 2:
        send(CommandType::Next);
        break;
    case 3:
        page = Page::Details;
        break;
    case 4:
        page = Page::Settings;
        break;
    case 5:
        page = view.setup ? Page::Setup : Page::Overview;
        break;
    case 6:
        send(CommandType::Setup);
        break;
    case 7:
        send(CommandType::CancelSetup);
        break;
    case 8:
        page = Page::Forget;
        break;
    case 9:
        send(CommandType::Forget);
        break;
    case 11:
        page = Page::SetupLink;
        break;
    case 12:
        page = Page::Setup;
        break;
    case 10: {
        Command c{};
        c.type = CommandType::SavePreferences;
        c.preferences.brightness = lv_slider_get_value(brightness_slider);
        c.preferences.dim_seconds = lv_slider_get_value(dim_slider);
        xQueueSend(commands, &c, 0);
        page = view.setup ? Page::Setup : Page::Overview;
        break;
    }
    }
}
lv_obj_t *button(lv_obj_t *parent, const char *s, int x, int y, int w, int action) {
    auto *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 44);
    lv_obj_set_style_bg_color(b, color(0x282a27), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(action)));
    auto *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, color(Text), 0);
    lv_label_set_text(l, s);
    lv_obj_center(l);
    return b;
}
void slider_change(lv_event_t *) {
    char b[80];
    snprintf(b, sizeof b, "Brightness %ld%%   Dim after %lds",
             long(lv_slider_get_value(brightness_slider)), long(lv_slider_get_value(dim_slider)));
    set(prefs_label, b);
    board::brightness(lv_slider_get_value(brightness_slider));
    last_brightness = lv_slider_get_value(brightness_slider);
}
void build() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, color(Bg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    title = subtitle = status_label = role = position = model = throughput = power = available =
        message = details = brightness_slider = dim_slider = prefs_label = nullptr;
    setup_qr = nullptr;
    qr_payload[0] = 0;
    auto *brand = label(screen, 24, 24, 260, &lv_font_montserrat_20, Amber);
    set(brand, "sparkDash");
    button(screen,
           page == Page::Overview ? "Settings"
           : setup_page()         ? "Cancel"
                                  : "Back",
           342, 16, 114,
           page == Page::Overview ? 4
           : setup_page()         ? 7
                                  : 5);
    status_label = label(screen, 24, 440, 432, &lv_font_montserrat_16, Muted);
    if (page == Page::Overview) {
        title = label(screen, 24, 58, 238, &lv_font_montserrat_32);
        role = label(screen, 266, 64, 190, &lv_font_montserrat_16, Green);
        lv_obj_set_style_text_align(role, LV_TEXT_ALIGN_RIGHT, 0);
        subtitle = label(screen, 24, 96, 432, &lv_font_montserrat_20, Muted);
        const char *names[] = {"GPU alloc.", "Temperature", "GPU usage"};
        for (int i = 0; i < 3; i++) {
            int y = 126 + i * 44;
            auto *l = label(screen, 24, y, 154, &lv_font_montserrat_20, Muted);
            set(l, names[i]);
            values[i] = label(screen, 170, y, 286, &lv_font_montserrat_20);
            lv_obj_set_style_text_align(values[i], LV_TEXT_ALIGN_RIGHT, 0);
            bars[i] = lv_bar_create(screen);
            lv_obj_set_pos(bars[i], 24, y + 27);
            lv_obj_set_size(bars[i], 432, 7);
            lv_obj_set_style_bg_color(bars[i], color(0x343634), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bars[i], color(Amber), LV_PART_INDICATOR);
            lv_bar_set_range(bars[i], 0, 100);
        }
        auto *l = label(screen, 24, 266, 212, &lv_font_montserrat_16, Muted);
        set(l, "GPU power");
        l = label(screen, 250, 266, 206, &lv_font_montserrat_16, Muted);
        set(l, "Available memory");
        power = label(screen, 24, 288, 222, &lv_font_montserrat_20);
        available = label(screen, 250, 288, 206, &lv_font_montserrat_24);
        model = label(screen, 24, 324, 432, &lv_font_montserrat_20, Amber);
        throughput = label(screen, 24, 357, 432, &lv_font_montserrat_24);
        button(screen, "<", 24, 392, 56, 1);
        position = label(screen, 96, 404, 100, &lv_font_montserrat_20, Muted);
        button(screen, "Details", 200, 392, 156, 3);
        button(screen, ">", 400, 392, 56, 2);
    } else if (page == Page::Details) {
        title = label(screen, 24, 72, 432, &lv_font_montserrat_24);
        auto *container = lv_obj_create(screen);
        lv_obj_set_pos(container, 24, 112);
        lv_obj_set_size(container, 432, 316);
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_scroll_dir(container, LV_DIR_VER);
        details = label(container, 0, 0, 410, &lv_font_montserrat_20);
        lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
    } else if (page == Page::Settings) {
        message = label(screen, 24, 72, 432, &lv_font_montserrat_16, Muted);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(message, 112);
        prefs_label = label(screen, 24, 188, 432, &lv_font_montserrat_16);
        brightness_slider = lv_slider_create(screen);
        lv_obj_set_pos(brightness_slider, 40, 230);
        lv_obj_set_size(brightness_slider, 400, 14);
        lv_slider_set_range(brightness_slider, 10, 100);
        lv_slider_set_value(brightness_slider, view.preferences.brightness, LV_ANIM_OFF);
        dim_slider = lv_slider_create(screen);
        lv_obj_set_pos(dim_slider, 40, 282);
        lv_obj_set_size(dim_slider, 400, 14);
        lv_slider_set_range(dim_slider, 30, 600);
        lv_slider_set_value(dim_slider, view.preferences.dim_seconds, LV_ANIM_OFF);
        lv_obj_add_event_cb(brightness_slider, slider_change, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(dim_slider, slider_change, LV_EVENT_VALUE_CHANGED, nullptr);
        slider_change(nullptr);
        button(screen, "Save display", 24, 330, 200, 10);
        button(screen, "Reconfigure", 236, 330, 220, 6);
        button(screen, "Forget connection", 24, 386, 432, 8);
    } else if (setup_page()) {
        auto *heading = label(screen, 24, 64, 432, &lv_font_montserrat_20);
        set(heading,
            page == Page::Setup ? "1. Scan to join setup Wi-Fi" : "2. Scan to open setup page");
        setup_qr = lv_qrcode_create(screen);
        lv_qrcode_set_size(setup_qr, 256);
        lv_obj_set_pos(setup_qr, 112, 92);
        lv_qrcode_set_dark_color(setup_qr, lv_color_black());
        lv_qrcode_set_light_color(setup_qr, lv_color_white());
        lv_qrcode_set_quiet_zone(setup_qr, true);
        message = label(screen, 24, 352, 432, &lv_font_montserrat_16);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_height(message, 38);
        button(screen, page == Page::Setup ? "Next: setup page" : "Back: join Wi-Fi", 24, 392, 288,
               page == Page::Setup ? 11 : 12);
        button(screen, "Settings", 324, 392, 132, 4);
    } else {
        message = label(screen, 24, 108, 432, &lv_font_montserrat_24);
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        set(message, "Forget saved Wi-Fi and server settings?\n\nThis opens setup again.");
        button(screen, "Keep settings", 24, 330, 206, 5);
        button(screen, "Forget", 250, 330, 206, 9);
    }
    built = page;
}
void pair(char *out, size_t n, spark::Value a, spark::Value b, bool memory) {
    char x[40], y[40];
    if (memory) {
        spark::format_memory(x, sizeof x, a);
        spark::format_memory(y, sizeof y, b);
    } else {
        spark::format_value(x, sizeof x, a, "", 1);
        spark::format_value(y, sizeof y, b, " W", 1);
    }
    snprintf(out, n, "%s / %s", x, y);
}
void tick(lv_timer_t *) {
    snapshot(view);
    if (view.setup && page == Page::Overview)
        page = Page::Setup;
    else if (!view.setup && setup_page())
        page = Page::Overview;
    if (page != built)
        build();
    char b[2048], a[96], c[96];
    auto &n = view.node;
    if (view.setup)
        spark::copy_text(b, sizeof b, view.status);
    else if (!view.connected)
        spark::copy_text(b, sizeof b, "Wi-Fi disconnected; retrying");
    else if (*n.error)
        snprintf(b, sizeof b, "%s | received %llus ago", n.error,
                 (unsigned long long)((now_ms() - n.received_ms) / 1000));
    else if (strcmp(view.status, "Connected"))
        spark::copy_text(b, sizeof b, view.status);
    else if (n.received)
        snprintf(b, sizeof b, "Received %llus ago",
                 (unsigned long long)((now_ms() - n.received_ms) / 1000));
    else
        spark::copy_text(b, sizeof b, "Waiting for metrics");
    set(status_label, b);
    if (page == Page::Overview) {
        set(title, view.count ? n.id : view.listed ? "No nodes" : "Connecting");
        set(subtitle, view.count ? n.name : view.status);
        snprintf(b, sizeof b, "%s / %s",
                 n.role == spark::Role::Head     ? "HEAD"
                 : n.role == spark::Role::Worker ? "WORKER"
                                                 : "NODE",
                 n.online == spark::Online::Online    ? "ONLINE"
                 : n.online == spark::Online::Offline ? "OFFLINE"
                                                      : "WAIT");
        set(role, view.count ? b : "");
        lv_obj_set_style_text_color(role,
                                    color(n.online == spark::Online::Online    ? Green
                                          : n.online == spark::Online::Offline ? Red
                                                                               : Muted),
                                    0);
        pair(b, sizeof b, n.used, n.total, true);
        set(values[0], b);
        spark::format_value(b, sizeof b, n.temperature, "°C");
        set(values[1], b);
        spark::format_value(b, sizeof b, n.usage, "%");
        set(values[2], b);
        spark::Value v[] = {n.percent, n.temperature, n.usage};
        for (int i = 0; i < 3; i++) {
            int val = v[i].valid ? int(std::clamp(v[i].value, 0., 100.)) : 0;
            lv_bar_set_value(bars[i], val, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(bars[i], color(val > 85 ? Red : Amber), LV_PART_INDICATOR);
        }
        pair(b, sizeof b, n.power, n.power_limit, false);
        set(power, b);
        spark::format_memory(b, sizeof b, n.available);
        set(available, b);
        set(model, n.role == spark::Role::Worker ? (*n.worker ? n.worker : "Distributed worker")
                                                 : (*n.model ? n.model : "LLM unavailable"));
        if (n.role == spark::Role::Worker) {
            snprintf(b, sizeof b, "%s%s", *n.head ? "Worker of " : "", n.head);
        } else {
            spark::format_value(a, sizeof a, n.generation, " tok/s");
            spark::format_value(c, sizeof c, n.prefill, " prefill/s");
            snprintf(b, sizeof b, "%s     %s", a, c);
        }
        set(throughput, b);
        snprintf(b, sizeof b, "%u / %u", unsigned(view.count ? view.selected + 1 : 0),
                 unsigned(view.count));
        set(position, b);
    } else if (page == Page::Details) {
        set(title, n.name);
        char mem[96], disk[96], rx[48], tx[48], cpu[48], temp[48], gpu[48], avail[48], watts[96],
            gen[48], prefill[48];
        pair(mem, sizeof mem, n.used, n.total, true);
        pair(disk, sizeof disk, n.disk_used, n.disk_total, true);
        spark::format_rate(rx, sizeof rx, n.rx);
        spark::format_rate(tx, sizeof tx, n.tx);
        spark::format_value(cpu, sizeof cpu, n.cpu_usage, "%");
        spark::format_value(temp, sizeof temp, n.cpu_temp, "°C");
        spark::format_value(gpu, sizeof gpu, n.usage, "%");
        spark::format_memory(avail, sizeof avail, n.available);
        pair(watts, sizeof watts, n.power, n.power_limit, false);
        spark::format_value(gen, sizeof gen, n.generation, " tok/s", 1);
        spark::format_value(prefill, sizeof prefill, n.prefill, " tok/s", 1);
        snprintf(b, sizeof b,
                 "%s\n%s\n\nGPU allocation\n%s\nAvailable: %s\nGPU usage: %s\nPower: %s\n\nCPU "
                 "usage: %s\nCPU temperature: %s\n\nRoot storage\n%s\n\nNetwork: %s\nRX: %s\nTX: "
                 "%s\n\n%s\n%s\n%s\nGeneration: %s\nPrefill: %s",
                 n.role == spark::Role::Head     ? "Head"
                 : n.role == spark::Role::Worker ? "Worker"
                                                 : "Standalone",
                 n.online == spark::Online::Online    ? "Online"
                 : n.online == spark::Online::Offline ? "Offline"
                                                      : "Waiting",
                 mem, avail, gpu, watts, cpu, temp, disk, n.iface, rx, tx, n.backend,
                 n.role == spark::Role::Worker ? n.worker : n.model, n.head, gen, prefill);
        set(details, b);
    } else if (page == Page::Settings) {
        snprintf(b, sizeof b,
                 "%s\nWi-Fi: %s (%d dBm)\nIP: %s | Firmware 0.1.0\nHeap: %u KiB | Errors: %u%s%s",
                 view.url, view.ssid, view.rssi, view.ip,
                 unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 unsigned(view.errors), view.overflow ? " | First 16 nodes only" : "",
                 view.config_error ? " | Configuration error" : "");
        set(message, b);
    } else if (setup_page()) {
        // Setup credentials are generated from a restricted alphabet (no QR delimiters).
        // Render only when the session/page changes, keeping allocation work out of each tick.
        if (page == Page::Setup) {
            snprintf(b, sizeof b, "WIFI:T:WPA;S:%s;P:%s;;", view.ap_ssid, view.ap_password);
            snprintf(a, sizeof a, "%s\nPassword: %s", view.ap_ssid, view.ap_password);
        } else {
            spark::copy_text(b, sizeof b, "http://192.168.4.1");
            spark::copy_text(a, sizeof a, "After joining Wi-Fi, scan or open\nhttp://192.168.4.1");
        }
        set(message, a);
        if (strcmp(qr_payload, b)) {
            if (lv_qrcode_update(setup_qr, b, strlen(b)) == LV_RESULT_OK)
                spark::copy_text(qr_payload, sizeof qr_payload, b);
            else
                set(message, "QR unavailable; use manual setup details");
        }
    }

    unsigned desired = view.preferences.brightness;
    if (page != Page::Settings) {
        if (!view.setup && now_ms() - last_touch > uint64_t(view.preferences.dim_seconds) * 1000) {
            dimmed = true;
            desired = 10;
        } else if (dimmed)
            desired = 10;
        if (desired != last_brightness) {
            board::brightness(desired);
            last_brightness = desired;
        }
    }
}
bool touch_filter(bool down, int x, int y) {
    if (down && !was_down) {
        start_x = x;
        start_y = y;
        consume_touch = dimmed;
        if (dimmed) {
            dimmed = false;
            board::brightness(view.preferences.brightness);
            last_brightness = view.preferences.brightness;
        }
        last_touch = now_ms();
    }
    if (down)
        last_touch = now_ms();
    bool consume = consume_touch;
    if (!down && was_down) {
        consume_touch = false;
    }
    was_down = down;
    return consume;
}
// Capture the last pressed coordinate: many touch controllers report no coordinates on release.
int last_x = 0, last_y = 0;
bool filter(bool down, int x, int y) {
    bool previously = was_down;
    bool consumed = touch_filter(down, x, y);
    if (down) {
        last_x = x;
        last_y = y;
    } else if (previously && !consumed && page == Page::Overview && start_y > 55 && start_y < 390) {
        int dx = last_x - start_x, dy = last_y - start_y;
        if (abs(dx) >= 48 && abs(dx) > 1.5 * abs(dy))
            send(dx < 0 ? CommandType::Next : CommandType::Previous);
    }
    return consumed;
}
} // namespace
void ui_start() {
    if (board::lock()) {
        snapshot(view);
        last_touch = now_ms();
        page = view.setup ? Page::Setup : Page::Overview;
        build();
        board::set_touch_filter(filter);
        lv_timer_create(tick, 100, nullptr);
        tick(nullptr);
        board::unlock();
    }
}
} // namespace app
