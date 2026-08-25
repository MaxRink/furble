#ifndef FURBLE_SIM_CLOCK_H
#define FURBLE_SIM_CLOCK_H

#include <cstdint>

namespace Furble::Sim {

uint32_t clockMillis(void);
void setClockMillis(uint32_t milliseconds);
void advanceClock(uint32_t milliseconds);

/** Return the elapsed milliseconds across the uint32_t clock wrap. */
constexpr uint32_t clockElapsed(uint32_t now, uint32_t since) {
  return now - since;
}

/** Return true when a clock deadline has been reached, including equality. */
constexpr bool clockDeadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace Furble::Sim

#endif
