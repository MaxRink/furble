#ifndef FURBLE_COMPANION_ADVERTISING_H
#define FURBLE_COMPANION_ADVERTISING_H

#include <cstdint>

namespace Furble {
namespace CompanionAdvertising {

/**
 * Fast reconnect interval, 100 ms in 0.625 ms units. A 1 s interval was
 * invisible to some macOS scan windows, so a host that is actively looking
 * gets this cadence.
 */
constexpr uint16_t FAST_INTERVAL = 160;
/** Slow reconnect interval, 1022.5 ms, Apple's recommended fallback step. */
constexpr uint16_t SLOW_INTERVAL = 1636;
/** How long the fast cadence lasts after enable, boot, disconnect or a press. */
constexpr uint64_t FAST_WINDOW_MS = 60000;

/**
 * Reconnect advertising cadence.
 *
 * Reconnect advertising runs until the bonded host returns, which can be
 * hours. Advertise fast for a window after an event that makes a reconnect
 * likely, then fall back to the slow interval. Not thread safe: the owner
 * guards it with its own mutex.
 */
class Cadence {
 public:
  /** Open or extend the fast window. */
  void boost(uint64_t nowMs) { m_FastUntilMs = nowMs + FAST_WINDOW_MS; }

  /** Interval to advertise at, in 0.625 ms units. */
  uint16_t interval(uint64_t nowMs) const {
    return (nowMs < m_FastUntilMs) ? FAST_INTERVAL : SLOW_INTERVAL;
  }

  /** True when advertising at running must restart at a new interval. */
  bool needsRestart(uint16_t running, uint64_t nowMs) const { return running != interval(nowMs); }

 private:
  uint64_t m_FastUntilMs = 0;
};

}  // namespace CompanionAdvertising
}  // namespace Furble

#endif
