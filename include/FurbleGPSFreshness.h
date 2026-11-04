#ifndef FURBLE_GPS_FRESHNESS_H
#define FURBLE_GPS_FRESHNESS_H

#include <cstdint>

namespace Furble {
namespace GPSFreshness {

/**
 * A reported fix needs a valid position, a valid GGA quality, and a location
 * younger than the freshness bound. TinyGPSPlus keeps the last valid location
 * and quality indefinitely, so the age is part of the status contract.
 */
constexpr bool fixIsCurrent(bool positionValid,
                            bool qualityValid,
                            uint32_t locationAgeMs,
                            uint32_t maxAgeMs) {
  return positionValid && qualityValid && (locationAgeMs < maxAgeMs);
}

}  // namespace GPSFreshness
}  // namespace Furble

#endif
