#ifndef FURBLE_AUTO_OFF_H
#define FURBLE_AUTO_OFF_H

#include <cstdint>

namespace Furble::AutoOff {

/** Return true when the disconnected idle auto-off policy may run. */
constexpr bool shouldPowerOff(uint8_t minutes,
                              bool disconnected,
                              bool scanActive,
                              uint32_t inactiveMilliseconds,
                              bool charging,
                              bool allowWhileCharging) {
  if ((minutes == 0) || !disconnected || scanActive || (charging && !allowWhileCharging)) {
    return false;
  }
  return inactiveMilliseconds >= (static_cast<uint32_t>(minutes) * 60000U);
}

/**
 * How long an automatic path waits after a refused power off before asking
 * again.
 *
 * A refusal means the SD writer did not settle, and every attempt blocks the UI
 * task for the whole handshake (up to 8 s). Without this the auto-off and
 * low-battery paths retry on the next one-second pass, so the device would stay
 * on with its UI stalled almost continuously. The value is a chosen bound, not a
 * measurement: a wedge that clears inside half a minute is retried once, and a
 * permanent one costs one handshake every 30 s instead of one per second.
 */
constexpr uint32_t POWER_OFF_RETRY_MS = 30000;

/** The tick at which a refused power off may be retried. */
constexpr uint32_t retryAfter(uint32_t nowMilliseconds) {
  return nowMilliseconds + POWER_OFF_RETRY_MS;
}

/**
 * Return true when a refused power off is still backing off.
 *
 * The difference is read as signed, the same wrap-safe idiom the rest of the
 * tree uses, so a retry point past the 32-bit tick wrap still holds.
 */
constexpr bool retryBlocked(uint32_t nowMilliseconds, uint32_t retryAtMilliseconds) {
  return static_cast<int32_t>(nowMilliseconds - retryAtMilliseconds) < 0;
}

}  // namespace Furble::AutoOff

#endif
