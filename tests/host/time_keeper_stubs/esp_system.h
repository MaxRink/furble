#ifndef FURBLE_HOST_TIME_KEEPER_ESP_SYSTEM_H
#define FURBLE_HOST_TIME_KEEPER_ESP_SYSTEM_H

// Keep the settings stub's esp_restart() and esp_err_to_name().
#include_next <esp_system.h>

typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
  ESP_RST_USB,
  ESP_RST_JTAG,
  ESP_RST_EFUSE,
  ESP_RST_PWR_GLITCH,
  ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

/** The test sets the reason the next TimeKeeper::init() observes. */
inline esp_reset_reason_t &hostTimeResetReason(void) {
  static esp_reset_reason_t reason = ESP_RST_POWERON;
  return reason;
}

inline esp_reset_reason_t esp_reset_reason(void) {
  return hostTimeResetReason();
}

#endif
