#ifndef FURBLE_SIM_ESP_PM_H
#define FURBLE_SIM_ESP_PM_H

#include <array>
#include <cstdint>
#include <cstdio>

#include "esp_err.h"

typedef struct {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
} esp_pm_config_t;

typedef enum {
  ESP_PM_CPU_FREQ_MAX,
  ESP_PM_APB_FREQ_MAX,
  ESP_PM_NO_LIGHT_SLEEP,
} esp_pm_lock_type_t;

namespace Furble::Sim {
void profilerPowerConfig(int max_frequency_mhz, int min_frequency_mhz, bool light_sleep_enabled);
void profilerPowerLockAcquire(int lock_type, const char *lock_name, const char *owner);
void profilerPowerLockRelease(int lock_type, const char *lock_name, const char *owner);
}  // namespace Furble::Sim

struct esp_pm_lock {
  esp_pm_lock_type_t type;
  const char *name;
  uint32_t count;
};

typedef struct esp_pm_lock *esp_pm_lock_handle_t;

inline esp_pm_config_t furble_sim_pm_config = {160, 40, true};
inline std::array<uint32_t, 3> furble_sim_pm_lock_counts = {0, 0, 0};
inline const char *furble_sim_pm_owner = nullptr;

inline esp_err_t esp_pm_configure(const esp_pm_config_t *config) {
  if (config == nullptr || config->max_freq_mhz < config->min_freq_mhz
      || config->min_freq_mhz <= 0) {
    return ESP_FAIL;
  }
  furble_sim_pm_config = *config;
  Furble::Sim::profilerPowerConfig(config->max_freq_mhz, config->min_freq_mhz,
                                   config->light_sleep_enable);
  return ESP_OK;
}

inline esp_err_t esp_pm_get_configuration(esp_pm_config_t *config) {
  if (config == nullptr) {
    return ESP_FAIL;
  }
  *config = furble_sim_pm_config;
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,
                                    int,
                                    const char *name,
                                    esp_pm_lock_handle_t *out_handle) {
  if (out_handle == nullptr) {
    return ESP_FAIL;
  }
  auto *lock = new esp_pm_lock {type, name == nullptr ? "unnamed" : name, 0};
  *out_handle = lock;
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
  delete handle;
  return ESP_OK;
}

inline void esp_pm_sim_set_owner(const char *owner) {
  furble_sim_pm_owner = owner;
}

inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
  if (handle == nullptr) {
    return ESP_FAIL;
  }
  handle->count++;
  furble_sim_pm_lock_counts[static_cast<size_t>(handle->type)]++;
  Furble::Sim::profilerPowerLockAcquire(
      static_cast<int>(handle->type), handle->name,
      furble_sim_pm_owner == nullptr ? handle->name : furble_sim_pm_owner);
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
  if (handle == nullptr) {
    return ESP_FAIL;
  }
  if (handle->count > 0) {
    handle->count--;
  }
  auto &count = furble_sim_pm_lock_counts[static_cast<size_t>(handle->type)];
  if (count > 0) {
    count--;
  }
  Furble::Sim::profilerPowerLockRelease(
      static_cast<int>(handle->type), handle->name,
      furble_sim_pm_owner == nullptr ? handle->name : furble_sim_pm_owner);
  return ESP_OK;
}

inline bool esp_pm_sim_light_sleep_allowed(void) {
  return furble_sim_pm_config.light_sleep_enable
         && (furble_sim_pm_lock_counts[ESP_PM_NO_LIGHT_SLEEP] == 0)
         && (furble_sim_pm_lock_counts[ESP_PM_CPU_FREQ_MAX] == 0)
         && (furble_sim_pm_lock_counts[ESP_PM_APB_FREQ_MAX] == 0);
}

inline int esp_pm_sim_cpu_frequency(void) {
  if (furble_sim_pm_lock_counts[ESP_PM_CPU_FREQ_MAX] > 0) {
    return furble_sim_pm_config.max_freq_mhz;
  }
  return 80;
}

inline esp_err_t esp_pm_dump_locks(FILE *output) {
  if (output == nullptr) {
    return ESP_FAIL;
  }
  std::fprintf(output, "sim power locks: cpu=%u apb=%u no-light-sleep=%u\n",
               static_cast<unsigned>(furble_sim_pm_lock_counts[ESP_PM_CPU_FREQ_MAX]),
               static_cast<unsigned>(furble_sim_pm_lock_counts[ESP_PM_APB_FREQ_MAX]),
               static_cast<unsigned>(furble_sim_pm_lock_counts[ESP_PM_NO_LIGHT_SLEEP]));
  return ESP_OK;
}

#endif
