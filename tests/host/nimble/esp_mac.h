#ifndef FURBLE_HOST_ESP_MAC_H
#define FURBLE_HOST_ESP_MAC_H

#include <cstddef>
#include <cstdint>

inline int esp_efuse_mac_get_default(uint8_t *mac) {
  if (mac != nullptr) {
    const uint8_t value[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    for (size_t i = 0; i < sizeof(value); i++) {
      mac[i] = value[i];
    }
  }
  return 0;
}

#endif
