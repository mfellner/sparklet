cmake_minimum_required(VERSION 3.16)
# Shared platform contract first (flash layout, bootloader options), then app settings.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults.platform;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.1.0")
project(sparkdash)
# Sparklet is installed in the platform's `sparklet` OTA slot next to the launcher:
# flash with `idf.py sparklet-flash`; `idf.py flash` is disabled because it would overwrite the launcher.
platform_app_slot(sparklet)
