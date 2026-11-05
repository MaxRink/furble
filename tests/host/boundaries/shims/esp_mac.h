#ifndef FURBLE_BOUNDARY_ESP_MAC_H
#define FURBLE_BOUNDARY_ESP_MAC_H

#include <cstdint>

#include "esp_err.h"

enum esp_mac_type_t {
  ESP_MAC_WIFI_STA = 0,
  ESP_MAC_BT = 1,
  ESP_MAC_ETH = 2,
};

#ifdef __cplusplus
extern "C" {
#endif

inline esp_err_t esp_efuse_mac_get_default(uint8_t *mac) {
  if (mac != nullptr) {
    const uint8_t value[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
    for (size_t index = 0; index < sizeof(value); index++) {
      mac[index] = value[index];
    }
  }
  return ESP_OK;
}

inline esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
  (void)type;
  return esp_efuse_mac_get_default(mac);
}

#ifdef __cplusplus
}
#endif

#endif
