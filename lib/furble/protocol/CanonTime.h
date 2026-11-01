#ifndef FURBLE_PROTOCOL_CANON_TIME_H
#define FURBLE_PROTOCOL_CANON_TIME_H

#include <array>
#include <cstdint>

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
  if ((year < 1970) || (month < 1) || (month > 12) || (day < 1) || (hour > 23) || (minute > 59)
      || (second > 59)) {
    return 0;
  }
  static constexpr std::array<uint8_t, 12> DAYS = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
  const uint8_t monthDays = DAYS[month - 1] + ((month == 2 && leap) ? 1 : 0);
  if (day > monthDays) {
    return 0;
  }

  uint32_t days = 0;
  for (uint32_t current = 1970; current < year; current++) {
    const bool yearLeap =
        ((current % 4) == 0) && (((current % 100) != 0) || ((current % 400) == 0));
    days += yearLeap ? 366 : 365;
  }
  for (uint8_t current = 1; current < month; current++) {
    days += DAYS[current - 1];
    if ((current == 2) && leap) {
      days++;
    }
  }
  days += day - 1;
  return (days * 24U * 60U * 60U) + (hour * 60U * 60U) + (minute * 60U) + second;
}

}  // namespace CanonProtocol
}  // namespace Furble

#endif  // FURBLE_PROTOCOL_CANON_TIME_H
