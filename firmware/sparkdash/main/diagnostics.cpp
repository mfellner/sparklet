#include "app.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstring>

namespace app {
// Deliberately small USB-only diagnostic protocol; never a shell or credential channel.
// Enables repeatable soak navigation without pretending to test physical touch.
static void diagnostics_task(void *) {
    char line[32]{};
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(100)) != 1)
            continue;
        if (c == '\r')
            continue;
        if (c == '\n') {
            line[used] = 0;
            if (!overflow && !strcmp(line, "NEXT"))
                send(CommandType::Next);
            else if (!overflow && !strcmp(line, "PREV"))
                send(CommandType::Previous);
            else if (!overflow && !strcmp(line, "STATUS")) {
                View v;
                snapshot(v);
                ESP_LOGI("diagnostics",
                         "uptime_ms=%llu nodes=%u selected=%u connected=%u requests=%u errors=%u "
                         "heap=%u largest=%u stack=%u net_stack=%u ui_stack=%u",
                         (unsigned long long)now_ms(), unsigned(v.count), unsigned(v.selected),
                         unsigned(v.connected), unsigned(v.requests), unsigned(v.errors),
                         unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                         unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                         unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                         unsigned(network_stack_free.load()), unsigned(ui_stack_free.load()));
            }
            used = 0;
            overflow = false;
        } else if (used < sizeof(line) - 1 && !overflow)
            line[used++] = c;
        else
            overflow = true;
    }
}
void diagnostics_start() {
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config{};
        config.rx_buffer_size = 256;
        config.tx_buffer_size = 256;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }
    assert(xTaskCreate(diagnostics_task, "spark_diag", 5120, nullptr, 2, nullptr) == pdPASS);
}
} // namespace app
