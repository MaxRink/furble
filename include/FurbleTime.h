#ifndef FURBLE_TIME_H
#define FURBLE_TIME_H

#include <cstdint>

namespace Furble::Time {

/** Return elapsed milliseconds using modulo-2^32 clock arithmetic. */
constexpr uint32_t elapsed(uint32_t now, uint32_t since) {
  return now - since;
}

/**
 * Return true once a deadline has been reached, including equality.
 *
 * The deadline must be less than half the uint32_t clock period away from
 * now. Furble's actual deadlines are many orders of magnitude shorter.
 */
constexpr bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace Furble::Time

#endif
