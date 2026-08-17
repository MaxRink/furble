#ifndef FURBLE_SIM_ESP_FLASH_H
#define FURBLE_SIM_ESP_FLASH_H

#include <cstdint>

#include <esp_err.h>

typedef struct esp_flash esp_flash_t;

inline esp_err_t esp_flash_get_size(esp_flash_t *, uint32_t *out_size) {
  if (out_size != nullptr) {
    *out_size = 8 * 1024 * 1024;
  }
  return ESP_OK;
}

#endif
