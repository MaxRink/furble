#ifndef FURBLE_PLATFORM_SHIM_ESP_ERR_H
#define FURBLE_PLATFORM_SHIM_ESP_ERR_H

// Extend the boundary esp_err.h shim with the two entries the platform uses and
// it does not declare: the esp_pm "not supported" error and ESP_ERROR_CHECK.
#include_next <esp_err.h>

#include <cstdlib>

#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

#ifndef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)              \
  do {                                  \
    esp_err_t error_check_result = (x); \
    if (error_check_result != ESP_OK) { \
      std::abort();                     \
    }                                   \
  } while (0)
#endif

#endif
