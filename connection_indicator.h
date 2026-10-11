#pragma once
#include <cstdint>

// Change phase every second; no delay, so UART continues to run.
inline bool connectionIndicatorOn(uint32_t now, bool wifiConnected, bool mqttConnected) {
  return wifiConnected && (mqttConnected || (now / 1000u) % 2u == 0);
}
