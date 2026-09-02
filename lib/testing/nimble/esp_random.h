#ifndef FURBLE_HOST_ESP_RANDOM_H
#define FURBLE_HOST_ESP_RANDOM_H
#include <cstdint>
inline uint32_t esp_random(void) {
  return 0x28u;
}
inline void esp_fill_random(void *data, size_t bytes) {
  auto *out = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < bytes; ++i)
    out[i] = static_cast<uint8_t>(i + 1);
}
#endif
