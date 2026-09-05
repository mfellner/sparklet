#include "app.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstring>

namespace app {
// Deliberately small USB-only diagnostic protocol; never a shell or credential channel.
// Enables repeatable soak navigation without pretending to test physical touch.
static void diagnostics_task(void *) {
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
    static char line[352]{};
#else
    char line[32]{};
#endif
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
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
            if (!overflow && (!strncmp(line, "TEST_URL ", 9) || !strcmp(line, "TEST_RESET"))) {
                Command command{};
                command.type = CommandType::TestUrl;
                spark::copy_text(command.connection.url, sizeof command.connection.url,
                                 !strcmp(line, "TEST_RESET") ? "" : line + 9);
                xQueueSend(commands, &command, 0);
            } else if (!overflow && !strcmp(line, "TEST_RECONNECT"))
                send(CommandType::TestReconnect);
            else
#endif
                if (!overflow && !strcmp(line, "NEXT"))
                send(CommandType::Next);
            else if (!overflow && !strcmp(line, "PREV"))
                send(CommandType::Previous);
            else if (!overflow && !strcmp(line, "STATUS")) {
                static View v;
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
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
                char id[193], error[385];
                spark::percent_encode(v.node.id, id, sizeof id);
                spark::percent_encode(v.status, error, sizeof error);
                ESP_LOGI("qa", "node=%s received=%u received_ms=%llu online=%u role=%u status=%s",
                         id, unsigned(v.node.received), (unsigned long long)v.node.received_ms,
                         unsigned(v.node.online), unsigned(v.node.role), error);
                static char source[961];
                spark::percent_encode(v.url, source, sizeof source);
                ESP_LOGI("qa_source", "%s", source);
                const spark::Value values[] = {
                    v.node.percent,     v.node.used,     v.node.total,      v.node.available,
                    v.node.temperature, v.node.usage,    v.node.power,      v.node.power_limit,
                    v.node.cpu_usage,   v.node.cpu_temp, v.node.disk_used,  v.node.disk_total,
                    v.node.rx,          v.node.tx,       v.node.generation, v.node.prefill};
                const char *keys[] = {"percent",     "used",     "total",      "available",
                                      "temperature", "usage",    "power",      "power_limit",
                                      "cpu_usage",   "cpu_temp", "disk_used",  "disk_total",
                                      "rx",          "tx",       "generation", "prefill"};
                for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
                    ESP_LOGI("qa_value", "%s=%.6f valid=%u", keys[i], values[i].value,
                             unsigned(values[i].valid));
#endif
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
