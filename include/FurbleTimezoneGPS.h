#ifndef FURBLE_TIMEZONE_GPS_H
#define FURBLE_TIMEZONE_GPS_H

#include <cstdint>

namespace Furble {

/**
 * Bridge between the GPS receiver and the automatic time zone.
 *
 * This is deliberately separate from GPS.cpp and Control.cpp. It reads only
 * the public GPS snapshot and feeds a fresh wired fix into Timezone. Callers
 * are the UI housekeeping timer and the headless service loop.
 */
namespace TimezoneGPS {

/**
 * The freshness predicate shared with the GPS receiver.
 *
 * A fix is fresh only when the receiver is enabled, the fix is LIVE (not held
 * or extrapolated), the source is the wired UART, position and fix flags are
 * set, date and time are valid, and every age is strictly below maxAgeMs.
 */
inline bool freshWiredFix(bool enabled,
                          bool live,
                          bool wired,
                          bool positionValid,
                          bool haveFix,
                          bool dateValid,
                          bool timeValid,
                          uint32_t locationAgeMs,
                          uint32_t dateAgeMs,
                          uint32_t timeAgeMs,
                          uint32_t maxAgeMs) {
  return enabled && live && wired && positionValid && haveFix && dateValid && timeValid
         && (locationAgeMs < maxAgeMs) && (dateAgeMs < maxAgeMs) && (timeAgeMs < maxAgeMs);
}

/**
 * Feed one fresh wired fix, or expire an old derived zone.
 *
 * A fix is accepted only when the receiver is enabled, the current fix is LIVE
 * (not held or extrapolated), the source is the wired UART, and location, date,
 * and time are all within GPS::FRESH_FIX_MS. Returns true when the applied zone
 * changed.
 */
bool service(void);

}  // namespace TimezoneGPS
}  // namespace Furble

#endif
