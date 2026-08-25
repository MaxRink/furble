#ifndef FURBLE_SIM_ESP_SYSTEM_H
#define FURBLE_SIM_ESP_SYSTEM_H

#ifdef __cplusplus
#include <cstdlib>
#else
#include <stdlib.h>
#endif

#ifdef __cplusplus
inline void esp_restart(void) {
  std::exit(0);
}
#else
static inline void esp_restart(void) {
  exit(0);
}
#endif

typedef enum {
  ESP_RST_UNKNOWN = 0,
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
} esp_reset_reason_t;

static inline esp_reset_reason_t esp_reset_reason(void) {
  return ESP_RST_POWERON;
}

static inline unsigned int esp_get_free_heap_size(void) {
  return 200 * 1024;
}

static inline unsigned int esp_get_minimum_free_heap_size(void) {
  return 100 * 1024;
}

#endif
