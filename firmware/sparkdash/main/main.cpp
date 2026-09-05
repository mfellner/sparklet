#include "app.hpp"
#include "board.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
extern "C" void app_main() {
    ESP_LOGI("sparkdash", "BOOT version=%s reset=%d", esp_app_get_description()->version,
             esp_reset_reason());
    board::init();
    app::network_start();
    app::ui_start();
    app::diagnostics_start();
}
