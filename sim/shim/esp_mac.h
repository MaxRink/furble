#ifndef FURBLE_SIM_ESP_MAC_H
#define FURBLE_SIM_ESP_MAC_H

#include <cstdint>

inline int esp_efuse_mac_get_default(uint8_t *mac) {
  for (uint8_t i = 0; i < 6; ++i) {
    mac[i] = static_cast<uint8_t>(0x20 + i);
  }
  return 0;
}

#endif
