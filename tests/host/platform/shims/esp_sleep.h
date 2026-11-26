#ifndef FURBLE_PLATFORM_TEST_ESP_SLEEP_H
#define FURBLE_PLATFORM_TEST_ESP_SLEEP_H

#include "esp_err.h"

// Light-sleep entry points that src/FurblePlatform.cpp configures. The host
// cannot sleep, so every call succeeds and records nothing: on the StickS3
// PMIC wake path the arm never reaches them (the SoC pin is not the wake
// source), and the PMIC side is modelled for real in sim/shim/M5PM1.h.
typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_TIMER = 3,
  ESP_SLEEP_WAKEUP_GPIO = 7,
} esp_sleep_source_t;

inline esp_err_t esp_sleep_enable_gpio_wakeup(void) {
  return ESP_OK;
}

inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) {
  return ESP_OK;
}

inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t) {
  return ESP_OK;
}

#endif  // FURBLE_PLATFORM_TEST_ESP_SLEEP_H
