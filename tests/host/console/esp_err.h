// Host esp_err shim for the console command suite.
//
// The guard matches tests/host/settings_stubs/esp_err.h so the two never
// define esp_err_t twice in one translation unit. No console target source
// reaches both this header and the NVS stub's error enum, which is where
// ESP_OK and friends live for the settings build.
#ifndef FURBLE_HOST_SETTINGS_ESP_ERR_H
#define FURBLE_HOST_SETTINGS_ESP_ERR_H

#include <cstdint>

using esp_err_t = int32_t;

enum {
  ESP_OK = 0,
  ESP_FAIL = -1,
  ESP_ERR_NO_MEM = 0x101,
  ESP_ERR_INVALID_ARG = 0x102,
  ESP_ERR_INVALID_STATE = 0x103,
  ESP_ERR_NOT_FOUND = 0x105,
};

const char *esp_err_to_name(esp_err_t error);

// The production console wraps every setup call in ESP_ERROR_CHECK. Abort on
// failure the way the device does, so a broken double is loud instead of silent.
#define ESP_ERROR_CHECK(expression) \
  do {                              \
    if ((expression) != ESP_OK) {   \
      __builtin_abort();            \
    }                               \
  } while (false)

#endif
