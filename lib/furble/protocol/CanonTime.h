#ifndef FURBLE_PROTOCOL_CANON_TIME_H
#define FURBLE_PROTOCOL_CANON_TIME_H

#include <cstdint>

#include "UtcTime.h"

namespace Furble {
namespace CanonProtocol {

/**
 * Convert a UTC broken-down time to a Unix epoch.
 *
 * Canon Camera Connect sends an absolute UTC epoch. mktime interprets the
 * struct tm in the runtime timezone, which shifts the packet on a non-UTC
 * device. This conversion has no timezone input.
 *
 * Returns 0 when the date or time is out of range.
 */
inline uint32_t utcEpoch(unsigned int year,
                         unsigned int month,
                         unsigned int day,
                         unsigned int hour,
                         unsigned int minute,
                         unsigned int second) {
  return UtcTime::epochSeconds(year, month, day, hour, minute, second);
}

}  // namespace CanonProtocol
}  // namespace Furble

#endif  // FURBLE_PROTOCOL_CANON_TIME_H
