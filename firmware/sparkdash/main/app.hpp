#pragma once
#include "core.hpp"
#include <mutex>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace app {
enum class CommandType { Previous, Next, Setup, CancelSetup, Forget, SavePreferences, Configure, Scan };
struct Command {CommandType type; spark::Connection connection{};spark::Preferences preferences{};};
struct View {spark::Node node{};size_t count=0,selected=0;bool overflow=false,listed=false,setup=false,connected=false,config_error=false;char status[128]="Starting",ssid[33]{},ip[20]{},url[320]{},ap_ssid[33]{},ap_password[17]{};spark::Preferences preferences{};int rssi=0;uint32_t revision=0,requests=0,errors=0;};
extern std::mutex mutex;
extern spark::Cache cache;
extern View state;
extern QueueHandle_t commands;
extern std::atomic<bool> wifi_connected,scan_done;
uint64_t now_ms();
void snapshot(View&);
bool send(CommandType);
void network_start();
void ui_start();
void status(const char*);
void portal_start();
void portal_stop();
}
