#include "app.hpp"
#include "app_switch.h"
#include "board.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
#include "esp_timer.h"
static void log_acceleration(const board::Acceleration &a) {
    static int64_t next_log = 0;
    const int64_t now = esp_timer_get_time() / 1000;
    if (now >= next_log) {
        ESP_LOGI("qa_imu", "x_mg=%d y_mg=%d z_mg=%d", int(a.x * 1000), int(a.y * 1000),
                 int(a.z * 1000));
        next_log = now + 1000;
    }
}
#endif
extern "C" void app_main() {
    ESP_LOGI("sparkdash", "BOOT version=%s reset=%d", esp_app_get_description()->version,
             esp_reset_reason());
    board::init();
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
    board::set_acceleration_observer(log_acceleration);
#endif
    // KEY short press or BOOT hold returns to the platform launcher.
    ESP_ERROR_CHECK(app_switch_buttons_start(nullptr, nullptr));
    app::network_start();
    app::ui_start();
    app::diagnostics_start();
    app_switch_mark_healthy();
}
