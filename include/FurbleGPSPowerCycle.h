#ifndef FURBLE_GPS_POWER_CYCLE_H
#define FURBLE_GPS_POWER_CYCLE_H

#include <cstdint>

namespace Furble {

// Bounded, self recovering retry policy for the GPS degraded state, shared
// between the GPS task and the host unit test so the test asserts the exact
// production behaviour. No FreeRTOS, hardware or NVS dependency, so it builds on
// the host.
//
// The burst windowed power management drops to a degraded state when it can no
// longer predict the receiver burst timing, for example after a run of bad
// checksum bursts or a failed interval measurement. That state used to latch
// forever and pin the NO_LIGHT_SLEEP lock, so a spell of bad reception cost the
// full lock current until the user toggled GPS off and on. This policy replaces
// the latch with a timed retry: the caller releases the lock, sleeps for a
// bounded backoff, then re-attempts acquisition. The backoff grows on repeated
// failures but is capped, so the state always keeps trying and recovers on its
// own once reception improves.
class GpsDegradedRetry {
 public:
  // First backoff before the initial retry, and the cap the backoff grows to.
  static constexpr uint32_t BACKOFF_MIN_MS = (10 * 1000);
  static constexpr uint32_t BACKOFF_MAX_MS = (5 * 60 * 1000);

  // Reset to the healthy state. A later entry counts as a fresh first failure.
  // The caller invokes this when it re-establishes a good burst prediction.
  void reset(void) {
    m_Active = false;
    m_Failures = 0;
    m_RetryDeadline = 0;
  }

  // Are we currently in the degraded retry state?
  bool active(void) const { return m_Active; }

  // Consecutive degraded entries since the last reset. One means the first.
  uint32_t failures(void) const { return m_Failures; }

  // Enter or re-enter the degraded state at tick now and schedule the next
  // retry. Returns true only on the first entry of a degraded episode, so the
  // caller emits a single WARN log. The retry deadline is always finite.
  bool enter(uint32_t now) {
    const bool first = !m_Active;
    const uint32_t backoff = backoffFor(m_Failures);
    m_Active = true;
    m_RetryDeadline = now + backoff;
    if (m_Failures < FAILURE_CAP) {
      m_Failures++;
    }
    return first;
  }

  // Absolute tick the next retry is due. Only meaningful while active.
  uint32_t retryDeadline(void) const { return m_RetryDeadline; }

  // Has the backoff elapsed so the caller should re-attempt acquisition?
  bool retryDue(uint32_t now) const {
    return m_Active && (static_cast<int32_t>(now - m_RetryDeadline) >= 0);
  }

  // Backoff scheduled by the most recent enter(), for logging and tests.
  uint32_t scheduledBackoff(void) const {
    return backoffFor(m_Failures > 0 ? (m_Failures - 1) : 0);
  }

 private:
  // The failure count is capped so the doubling never overflows.
  static constexpr uint32_t FAILURE_CAP = 16;

  // Exponential backoff from BACKOFF_MIN_MS, doubling per prior failure, capped
  // at BACKOFF_MAX_MS.
  static uint32_t backoffFor(uint32_t priorFailures) {
    uint32_t backoff = BACKOFF_MIN_MS;
    for (uint32_t i = 0; (i < priorFailures) && (backoff < BACKOFF_MAX_MS); i++) {
      const uint32_t doubled = backoff * 2;
      backoff = (doubled > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : doubled;
    }
    return backoff;
  }

  bool m_Active = false;
  uint32_t m_Failures = 0;
  uint32_t m_RetryDeadline = 0;
};

// Should the on-screen GPS status indicator show the degraded state? The status
// icon reuses this to layer a warning tint on its glyph so a degraded, self
// recovering GPS is visible on screen and not just on the console. Gated on
// enabled so a default off GPS never trips the indicator, and so a resync or a
// GPS off clears it. Kept here, free of LVGL, so the host test pins the mapping.
inline bool gpsIndicatorDegraded(bool enabled, bool degraded) {
  return enabled && degraded;
}

}  // namespace Furble

#endif
