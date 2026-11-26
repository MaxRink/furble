#ifndef FURBLE_PLATFORM_SHIM_ESP_PM_H
#define FURBLE_PLATFORM_SHIM_ESP_PM_H

#include <esp_err.h>
#include <cstdint>
#include <cstdio>

typedef struct {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
} esp_pm_config_t;

esp_err_t esp_pm_configure(const esp_pm_config_t *config);
esp_err_t esp_pm_get_configuration(esp_pm_config_t *config);
esp_err_t esp_pm_dump_locks(FILE *stream);

#endif
