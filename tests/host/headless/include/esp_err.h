#ifndef FURBLE_HEADLESS_ESP_ERR_H
#define FURBLE_HEADLESS_ESP_ERR_H

#include <cstdint>
#include <cstdlib>

using esp_err_t = int32_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif

#define ESP_ERROR_CHECK(expression) \
  do {                              \
    if ((expression) != ESP_OK) {   \
      std::abort();                 \
    }                               \
  } while (false)

#endif
