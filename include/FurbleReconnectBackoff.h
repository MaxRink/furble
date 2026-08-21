#ifndef FURBLE_RECONNECT_BACKOFF_H
#define FURBLE_RECONNECT_BACKOFF_H

#include <cstdint>

namespace Furble {

// Pure reconnect backoff timing, shared between the Control task and the host
// backoff unit test so the test asserts the exact production curve. No BLE,
// FreeRTOS or NVS dependency, so it builds on the host.
namespace ReconnectBackoff {

// Base wait between infinite-reconnect attempts and the first exponential
// backoff step.
static constexpr uint32_t BASE_MS = (5 * 1000);

// Upper bound on the backoff so a persistently unreachable camera still retries
// every couple of minutes instead of growing without limit.
static constexpr uint32_t MAX_MS = (120 * 1000);

// Wait before the first retry after a failed reconnect. A stale session is the
// common cause of that first failure: the camera still holds the previous BLE
// session for a short moment after a drop and releases it within a couple of
// seconds. Retry quickly so a session the camera clears in a few seconds
// reconnects in a few seconds, instead of stalling on the full backoff.
static constexpr uint32_t FIRST_RETRY_MS = (2500);

// Compute the wait before the next reconnect attempt. attempt is the number of
// retries already performed: 0 selects the short first retry. Later attempts
// hold BASE_MS when backoff is disabled, or grow exponentially from BASE_MS up
// to MAX_MS when it is enabled. The exponential curve for attempt >= 1 is
// unchanged from the original; only the first retry is shortened.
inline uint32_t delayMs(uint32_t attempt, bool backoffEnabled) {
  if (attempt == 0) {
    return FIRST_RETRY_MS;
  }

  if (!backoffEnabled) {
    return BASE_MS;
  }

  const uint32_t shift = attempt < 5 ? attempt : 5;
  const uint64_t delay = static_cast<uint64_t>(BASE_MS) << shift;
  return delay > MAX_MS ? MAX_MS : static_cast<uint32_t>(delay);
}

}  // namespace ReconnectBackoff
}  // namespace Furble

#endif
