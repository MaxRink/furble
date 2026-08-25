#ifndef FURBLE_HOST_ESP_MAC_H
#define FURBLE_HOST_ESP_MAC_H

#include <cstddef>
#include <cstdint>

enum esp_mac_type_t : uint8_t { ESP_MAC_BT = 0 };
constexpr int ESP_OK = 0;

inline int esp_efuse_mac_get_default(uint8_t *mac) {
  if (mac != nullptr) {
    const uint8_t value[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    for (size_t i = 0; i < sizeof(value); i++) {
      mac[i] = value[i];
    }
  }
  return 0;
}

inline int esp_read_mac(uint8_t *mac, esp_mac_type_t) {
  return esp_efuse_mac_get_default(mac);
}

#endif
