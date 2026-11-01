#ifndef FURBLE_PLATFORM_POLICY_H
#define FURBLE_PLATFORM_POLICY_H

#include <cstdint>

namespace Furble {
namespace PlatformPolicy {

/**
 * Minimum interval between M5PM1 button-state reads.
 *
 * The UI loop runs every 5 ms and used to read the PMIC button on every pass,
 * about 200 I2C transactions per second. Twenty milliseconds keeps a 50 Hz
 * sample while cutting the bus traffic tenfold. A human click is far longer
 * than 50 ms, so no edge is lost by the coarser cadence.
 */
constexpr uint32_t PM1_POLL_MS = 20;

/** True when the next PM1 poll is due, wrap-safe for the millisecond tick. */
constexpr bool pm1PollDue(uint32_t now, uint32_t last) {
  return static_cast<uint32_t>(now - last) >= PM1_POLL_MS;
}

}  // namespace PlatformPolicy
}  // namespace Furble

#endif  // FURBLE_PLATFORM_POLICY_H
