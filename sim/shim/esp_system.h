#ifndef FURBLE_SIM_ESP_SYSTEM_H
#define FURBLE_SIM_ESP_SYSTEM_H

#ifdef __cplusplus
#include <cstdlib>
#include "esp_heap_caps.h"
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
  ESP_RST_USB,
  ESP_RST_JTAG,
  ESP_RST_EFUSE,
  ESP_RST_PWR_GLITCH,
  ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

#ifdef __cplusplus
extern "C" uint32_t furble_sim_hw_reset_reason(void);
#else
uint32_t furble_sim_hw_reset_reason(void);
#endif

static inline esp_reset_reason_t esp_reset_reason(void) {
  return (esp_reset_reason_t)furble_sim_hw_reset_reason();
}

static inline unsigned int esp_get_free_heap_size(void) {
  return static_cast<unsigned int>(
      furble_sim_heap_free(static_cast<uint32_t>(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

static inline unsigned int esp_get_minimum_free_heap_size(void) {
  return static_cast<unsigned int>(
      furble_sim_heap_minimum(static_cast<uint32_t>(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

#endif
