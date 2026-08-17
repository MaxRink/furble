#ifndef FURBLE_SIM_ESP_CHIP_INFO_H
#define FURBLE_SIM_ESP_CHIP_INFO_H

#include <cstdint>

#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "sim"
#endif

typedef struct {
  uint16_t revision;
  uint8_t cores;
} esp_chip_info_t;

inline void esp_chip_info(esp_chip_info_t *info) {
  if (info != nullptr) {
    info->revision = 0;
    info->cores = 2;
  }
}

#endif
