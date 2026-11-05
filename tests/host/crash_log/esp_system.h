#ifndef FURBLE_HOST_CRASH_ESP_SYSTEM_H
#define FURBLE_HOST_CRASH_ESP_SYSTEM_H

#include <cstdint>

#include "esp_err.h"

typedef enum {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON = 1,
  ESP_RST_EXT = 2,
  ESP_RST_SW = 3,
  ESP_RST_PANIC = 4,
  ESP_RST_INT_WDT = 5,
  ESP_RST_TASK_WDT = 6,
  ESP_RST_WDT = 7,
  ESP_RST_DEEPSLEEP = 8,
  ESP_RST_BROWNOUT = 9,
  ESP_RST_SDIO = 10,
} esp_reset_reason_t;

typedef void (*shutdown_handler_t)(void);

/** Mutable per-test values. */
inline esp_reset_reason_t &hostCrashResetReason(void) {
  static esp_reset_reason_t reason = ESP_RST_POWERON;
  return reason;
}

inline shutdown_handler_t &hostCrashShutdownHandler(void) {
  static shutdown_handler_t handler = nullptr;
  return handler;
}

inline esp_reset_reason_t esp_reset_reason(void) {
  return hostCrashResetReason();
}

inline esp_err_t esp_register_shutdown_handler(shutdown_handler_t handler) {
  hostCrashShutdownHandler() = handler;
  return ESP_OK;
}

#endif
