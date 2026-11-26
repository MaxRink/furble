#ifndef FURBLE_PLATFORM_TEST_ESP_PM_H
#define FURBLE_PLATFORM_TEST_ESP_PM_H

#include <cstdint>
#include <cstdio>

#include "esp_err.h"

// Dynamic frequency scaling. src/FurblePower.cpp requests a configuration and
// takes locks through this header; the host records the request so the test can
// see it, and performs no frequency change.
typedef struct {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
} esp_pm_config_t;

typedef enum {
  ESP_PM_CPU_FREQ_MAX = 0,
  ESP_PM_APB_FREQ_MAX = 1,
  ESP_PM_NO_LIGHT_SLEEP = 2,
} esp_pm_lock_type_t;

struct esp_pm_lock {
  esp_pm_lock_type_t type;
  const char *name;
  uint32_t count;
};

typedef struct esp_pm_lock *esp_pm_lock_handle_t;

namespace Furble::PlatformTest {
inline esp_pm_config_t pmConfig = {160, 40, true};
inline uint32_t pmLockCount[3] = {0, 0, 0};
}  // namespace Furble::PlatformTest

inline esp_err_t esp_pm_configure(const esp_pm_config_t *config) {
  if ((config == nullptr) || (config->max_freq_mhz < config->min_freq_mhz)
      || (config->min_freq_mhz <= 0)) {
    return ESP_FAIL;
  }
  Furble::PlatformTest::pmConfig = *config;
  return ESP_OK;
}

inline esp_err_t esp_pm_get_configuration(esp_pm_config_t *config) {
  if (config == nullptr) {
    return ESP_FAIL;
  }
  *config = Furble::PlatformTest::pmConfig;
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,
                                    int,
                                    const char *name,
                                    esp_pm_lock_handle_t *out_handle) {
  if (out_handle == nullptr) {
    return ESP_FAIL;
  }
  // The IDF API hands back a raw handle; esp_pm_lock_delete owns the delete.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  *out_handle = new esp_pm_lock {type, name == nullptr ? "unnamed" : name, 0};
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
  delete handle;  // NOLINT(cppcoreguidelines-owning-memory): the create handle
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
  if (handle == nullptr) {
    return ESP_FAIL;
  }
  handle->count++;
  Furble::PlatformTest::pmLockCount[static_cast<size_t>(handle->type)]++;
  return ESP_OK;
}

inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
  if (handle == nullptr) {
    return ESP_FAIL;
  }
  if (handle->count > 0) {
    handle->count--;
  }
  auto &count = Furble::PlatformTest::pmLockCount[static_cast<size_t>(handle->type)];
  if (count > 0) {
    count--;
  }
  return ESP_OK;
}

inline esp_err_t esp_pm_dump_locks(FILE *) {
  return ESP_OK;
}

#endif  // FURBLE_PLATFORM_TEST_ESP_PM_H
