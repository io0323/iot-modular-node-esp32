// ESP-IDF の入口。中身は src/platform/esp32/app_main.cpp。
#include "platform/esp32/app_main.hpp"

extern "C" void app_main(void) { node::platform::esp32::run(); }
