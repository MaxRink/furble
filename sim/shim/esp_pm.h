#ifndef FURBLE_SIM_ESP_PM_H
#define FURBLE_SIM_ESP_PM_H

#include <cstdlib>

#include "esp_err.h"

typedef struct {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
} esp_pm_config_t;

inline esp_err_t esp_pm_configure(const esp_pm_config_t *) {
  return ESP_OK;
}

#endif
