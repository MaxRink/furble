// Host esp_pm shim for the console command suite.
//
// src/FurblePower.cpp is compiled for real here, so the lock API has to exist.
// Every call is a no-op: the host has no dynamic frequency scaling, and the
// console only reports the counters Power keeps itself.
#ifndef FURBLE_HOST_CONSOLE_ESP_PM_H
#define FURBLE_HOST_CONSOLE_ESP_PM_H

#include <cstdint>

#include "esp_err.h"

typedef enum {
  ESP_PM_CPU_FREQ_MAX,
  ESP_PM_APB_FREQ_MAX,
  ESP_PM_NO_LIGHT_SLEEP,
} esp_pm_lock_type_t;

typedef struct esp_pm_lock *esp_pm_lock_handle_t;

typedef struct {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
} esp_pm_config_t;

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,
                             int argument,
                             const char *name,
                             esp_pm_lock_handle_t *handle);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_configure(const void *config);
esp_err_t esp_pm_get_configuration(void *config);
void esp_pm_dump_locks(void *stream);

#endif
