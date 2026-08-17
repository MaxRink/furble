#ifndef FURBLE_SIM_ESP_RANDOM_H
#define FURBLE_SIM_ESP_RANDOM_H

#include <cstdint>

inline uint32_t esp_random(void) {
  return 0x28u;
}

#endif
