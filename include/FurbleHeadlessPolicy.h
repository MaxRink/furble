#ifndef FURBLE_HEADLESS_POLICY_H
#define FURBLE_HEADLESS_POLICY_H

#include <cstdint>

namespace Furble {
namespace HeadlessPolicy {

/**
 * Idle period of the headless UI loop.
 *
 * The headless build has no display, so its loop only pumps Platform::update(),
 * the queued console/companion requests, pending scan callbacks, and the 1 Hz
 * GPS service. A 5 ms tick was inherited from the display task and wakes the
 * CPU 200 times a second for no work. Fifty milliseconds is well below any
 * human console or companion latency, and the request queue is still drained
 * every iteration.
 */
constexpr uint32_t LOOP_MS = 50;

/** Floor for a shortened sleep so a near deadline cannot spin the loop. */
constexpr uint32_t MIN_SLEEP_MS = 1;

/**
 * Sleep before the next headless loop iteration.
 *
 * Never longer than LOOP_MS and never past the next GPS service deadline, so
 * the deadline-driven sleep does not shift the 1 Hz geotag cadence. Returns 0
 * when the service is already due; the caller services it and recomputes.
 */
constexpr uint32_t loopSleepMs(int64_t nowUs, int64_t nextGpsServiceUs) {
  constexpr int64_t LOOP_US = static_cast<int64_t>(LOOP_MS) * 1000;
  if (nextGpsServiceUs <= nowUs) {
    return 0;
  }

  const int64_t remainingUs = nextGpsServiceUs - nowUs;
  if (remainingUs >= LOOP_US) {
    return LOOP_MS;
  }

  const int64_t remainingMs = remainingUs / 1000;
  return (remainingMs < static_cast<int64_t>(MIN_SLEEP_MS)) ? MIN_SLEEP_MS
                                                            : static_cast<uint32_t>(remainingMs);
}

}  // namespace HeadlessPolicy
}  // namespace Furble

#endif  // FURBLE_HEADLESS_POLICY_H
