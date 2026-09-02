#ifndef FURBLE_SIM_ESP_MAC_H
#define FURBLE_SIM_ESP_MAC_H

#include <cstdint>

#include <esp_err.h>

typedef enum { ESP_MAC_BT = 0 } esp_mac_type_t;

inline int esp_efuse_mac_get_default(uint8_t *mac) {
  for (uint8_t i = 0; i < 6; ++i) {
    mac[i] = static_cast<uint8_t>(0x20 + i);
  }
  return 0;
}

inline int esp_read_mac(uint8_t *mac, esp_mac_type_t) {
  return esp_efuse_mac_get_default(mac);
}

#endif
