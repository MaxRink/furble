#ifndef FURBLE_HOST_MQTT_ESP_ERR_H
#define FURBLE_HOST_MQTT_ESP_ERR_H

using esp_err_t = int;

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;

inline const char *esp_err_to_name(esp_err_t err) {
  return err == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

#endif
