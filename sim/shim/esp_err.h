#ifndef FURBLE_SIM_ESP_ERR_H
#define FURBLE_SIM_ESP_ERR_H

#include <cstdlib>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110

#define ESP_ERROR_CHECK(expr) \
  do {                        \
    if ((expr) != ESP_OK) {   \
      std::abort();           \
    }                         \
  } while (false)

inline const char *esp_err_to_name(esp_err_t err) {
  return (err == ESP_OK) ? "ESP_OK" : "ESP_FAIL";
}

#endif
