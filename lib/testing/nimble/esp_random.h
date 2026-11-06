#ifndef FURBLE_HOST_ESP_RANDOM_H
#define FURBLE_HOST_ESP_RANDOM_H
#include <atomic>
#include <cstdint>
inline uint32_t esp_random(void) {
  static std::atomic<uint32_t> counter {0};
  return 0x28u + counter.fetch_add(1, std::memory_order_relaxed);
}
inline void esp_fill_random(void *data, size_t bytes) {
  static std::atomic<uint32_t> counter {0};
  const uint32_t seed = counter.fetch_add(1, std::memory_order_relaxed);
  auto *out = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < bytes; ++i)
    out[i] = static_cast<uint8_t>(i + 1 + seed);
}
#endif
