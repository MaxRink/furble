#ifndef FURBLE_HOST_SETTINGS_ESP_SYSTEM_H
#define FURBLE_HOST_SETTINGS_ESP_SYSTEM_H

#include "nvs.h"

inline void esp_restart() {}

inline const char *esp_err_to_name(esp_err_t error) {
  return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

#endif
