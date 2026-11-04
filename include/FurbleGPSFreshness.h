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

/** The fix verdict together with the exact age it was based on. */
struct fix_t {
  bool fix;
  uint32_t locationAgeMs;
};

/**
 * Sample the location age exactly once and derive both fields from it.
 *
 * getStatusSnapshot() used to call gps.location.age() twice, so a snapshot
 * could report fix=true with location_age=MAX_AGE_MS. This seam makes that
 * impossible and lets the host test count the age reads.
 */
template <typename AgeFn>
fix_t evaluate(bool positionValid, bool qualityValid, AgeFn ageFn, uint32_t maxAgeMs) {
  const uint32_t locationAgeMs = ageFn();
  return {fixIsCurrent(positionValid, qualityValid, locationAgeMs, maxAgeMs), locationAgeMs};
}

}  // namespace GPSFreshness
}  // namespace Furble

#endif
