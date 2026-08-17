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

typedef enum {
  ESP_PM_CPU_FREQ_MAX,
  ESP_PM_APB_FREQ_MAX,
  ESP_PM_NO_LIGHT_SLEEP,
} esp_pm_lock_type_t;

typedef struct esp_pm_lock *esp_pm_lock_handle_t;

inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t,
                                    int,
                                    const char *,
                                    esp_pm_lock_handle_t *out_handle) {
  static struct esp_pm_lock *dummy = reinterpret_cast<struct esp_pm_lock *>(0x1);
  if (out_handle != nullptr) {
    *out_handle = dummy;
  }
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t) {
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t) {
  return ESP_OK;
}

#endif
