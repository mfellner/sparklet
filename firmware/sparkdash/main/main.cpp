#include "app.hpp"
#include "board.hpp"
#include "esp_log.h"
#include "esp_system.h"
extern "C" void app_main() {
    ESP_LOGI("sparkdash", "BOOT version=0.1.0 reset=%d", esp_reset_reason());
    board::init();
    app::network_start();
    app::ui_start();
    app::diagnostics_start();
}
