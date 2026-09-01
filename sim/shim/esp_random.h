#ifndef FURBLE_SIM_ESP_RANDOM_H
#define FURBLE_SIM_ESP_RANDOM_H

#include <cstddef>
#include <cstdint>

// Deterministic stand-in for the hardware RNG. Production code uses it for
// device identifiers, so a single fixed value would give every FauxNY camera
// the same BLE address and the saved-camera index would collapse two cameras
// into one entry. Step a counter instead: reproducible across runs, distinct
// per call. The first value keeps the historical 0x28, so the first FauxNY
// camera still renders as "FauxNY-40" in the documentation captures.
inline uint32_t esp_random(void) {
  static uint32_t next = 0x28u;
  return next++;
}

inline void esp_fill_random(void *data, size_t bytes) {
  auto *out = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < bytes; ++i) {
    out[i] = static_cast<uint8_t>(i + 1);
  }
}

#endif
