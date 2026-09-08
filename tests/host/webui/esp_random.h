#ifndef FURBLE_HOST_WEBUI_ESP_RANDOM_H
#define FURBLE_HOST_WEBUI_ESP_RANDOM_H
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void *buffer, size_t length) {
  auto *bytes = static_cast<uint8_t *>(buffer);
  for (size_t index = 0; index < length; index++) bytes[index] = static_cast<uint8_t>(index + 1);
}
#endif
