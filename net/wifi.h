#pragma once
#include <zephyr/kernel.h>

namespace net {

// Join CONFIG_SIM_WIFI_SSID and wait for an IP (L4 connected).
// Same code for ESP32 (native Wi-Fi) and STM32 (eS-WiFi shield).
int wifi_connect(k_timeout_t timeout);

} // namespace net
