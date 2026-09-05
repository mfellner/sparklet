#pragma once
#include "core.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
#include <mutex>
namespace app {
enum class CommandType {
    Previous,
    Next,
    SelectionChanged,
    Setup,
    CancelSetup,
    Forget,
    SavePreferences,
    Configure,
    Scan,
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
    TestUrl,
    TestReconnect,
#endif
};
struct Command {
    CommandType type;
    spark::Connection connection{};
    spark::Preferences preferences{};
};
struct View {
    spark::Node node{};
    size_t count = 0, selected = 0;
    bool overflow = false, listed = false, setup = false, connected = false, config_error = false;
    char status[128] = "Starting", ssid[33]{}, ip[20]{}, url[320]{}, ap_ssid[33]{},
         ap_password[17]{};
    spark::Preferences preferences{};
    uint32_t preferences_save_result = 0;
    bool preferences_save_ok = false;
    int rssi = 0;
    uint32_t revision = 0, requests = 0, errors = 0;
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
    uint32_t navigation_sequence = 0, navigation_started = 0;
#endif
};
extern std::mutex mutex;
extern spark::Cache cache;
extern View state;
extern QueueHandle_t commands;
extern std::atomic<bool> wifi_connected, scan_done;
extern std::atomic<uint32_t> network_stack_free, ui_stack_free, ui_brightness, ui_dimmed;
extern std::atomic<uint32_t> portal_stack_free;
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
void portal_self_test();
char *portal_test_buffer();
void portal_test_buffer_release();
void navigation_self_test();
void rotation_self_test();
void preferences_self_test(bool enabled);
#endif
uint64_t now_ms();
void snapshot(View &);
bool send(CommandType);
void network_start();
void ui_start();
void diagnostics_start();
void status(const char *);
void portal_start();
void portal_stop();
} // namespace app
