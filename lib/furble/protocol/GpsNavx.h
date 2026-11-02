#ifndef FURBLE_PROTOCOL_GPSNAVX_H
#define FURBLE_PROTOCOL_GPSNAVX_H

#include <cstddef>
#include <cstdint>

namespace Furble::GpsNavx {

// CASIC CFG-NAVX payload offsets. The mask is at 0, dyModel at 4, minSVs at
// 6, and navSystem at 13. minSVs is deliberately named so a test can prove a
// constellation write does not clobber it.
constexpr size_t OFFSET_DY_MODEL = 4;
constexpr size_t OFFSET_MIN_SVS = 6;
constexpr size_t OFFSET_NAV_SYSTEM = 13;
constexpr size_t PAYLOAD_SIZE = 44;

constexpr uint32_t MASK_DY_MODEL = 1U << 0;
constexpr uint32_t MASK_NAV_SYSTEM = 1U << 8;

struct Config {
  bool constellationSet;
  uint8_t constellation;
  bool dynamicModelSet;
  uint8_t dynamicModel;
};

/**
 * Apply the selected CFG-NAVX fields to an existing query response.
 *
 * Writes the little-endian mask at offset 0 and returns it. Returns 0 without
 * touching the buffer when the payload is not the expected 44 bytes.
 */
inline uint32_t apply(uint8_t *payload, size_t size, const Config &config) {
  if ((payload == nullptr) || (size != PAYLOAD_SIZE)) {
    return 0;
  }
  uint32_t mask = 0;
  if (config.constellationSet) {
    mask |= MASK_NAV_SYSTEM;
    payload[OFFSET_NAV_SYSTEM] = config.constellation;
  }
  if (config.dynamicModelSet) {
    mask |= MASK_DY_MODEL;
    payload[OFFSET_DY_MODEL] = config.dynamicModel;
  }
  payload[0] = static_cast<uint8_t>(mask & 0xff);
  payload[1] = static_cast<uint8_t>((mask >> 8) & 0xff);
  payload[2] = static_cast<uint8_t>((mask >> 16) & 0xff);
  payload[3] = static_cast<uint8_t>((mask >> 24) & 0xff);
  return mask;
}

}  // namespace Furble::GpsNavx

#endif
