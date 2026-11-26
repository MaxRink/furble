#ifndef FURBLE_PLATFORM_TEST_ESP_SYSTEM_H
#define FURBLE_PLATFORM_TEST_ESP_SYSTEM_H

#include <cstdint>

#include "esp_timer.h"

// src/FurblePlatform.cpp reads the microsecond clock through esp_timer without
// including it directly, because the IDF esp_system.h does. Keep that visible
// here, and keep the restart and reset-reason surface the same.
enum esp_reset_reason_t {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON = 1,
  ESP_RST_SW = 3,
  ESP_RST_PANIC = 4,
  ESP_RST_BROWNOUT = 7,
};

inline esp_reset_reason_t esp_reset_reason(void) {
  return ESP_RST_POWERON;
}

inline void esp_restart(void) {}

#endif  // FURBLE_PLATFORM_TEST_ESP_SYSTEM_H
