#ifndef FURBLE_RTC_COUNTDOWN_H
#define FURBLE_RTC_COUNTDOWN_H

#include <cstdint>

namespace Furble {
namespace RtcCountdown {

/**
 * The BM8563 countdown, as the driver furble calls programs it.
 *
 * `M5.Rtc.setAlarmIRQ(int)` is M5Unified's `setTimerIRQ(msec)` divided back to
 * seconds, and its selection rules are in the pinned driver:
 *
 *   M5Unified src/utility/RTC_Class.hpp      setAlarmIRQ(int) -> setTimerIRQ()
 *   M5Unified src/utility/rtc/PCF8563_Class.cpp setTimerIRQ()
 *
 * Below 270 s the countdown runs at one-second resolution with control 0x82.
 * At or above it the request is rounded to the nearest minute, control 0x83,
 * and the count is clamped to the 8-bit countdown register. Because the round
 * is to nearest, a minute-mode request that is not itself a minute boundary
 * can program a *later* deadline; the firmware must reject that rather than
 * miss the shot.
 *
 * The BM8563 register map itself is recorded as "cited, not opened" in
 * docs/power/datasheet-register.md, so the register semantics come from the
 * M5Unified driver above and are UNVERIFIED against the BM8563 datasheet.
 */
constexpr int MINUTE_MODE_THRESHOLD_S = 270;
constexpr int COUNTER_MAX = 255;
constexpr int MINUTE_S = 60;

/** Control register 0x0E: enable the countdown at 1 Hz. */
constexpr uint8_t TIMER_CONTROL_SECONDS = 0x82;
/** Control register 0x0E: enable the countdown at 1/60 Hz. */
constexpr uint8_t TIMER_CONTROL_MINUTES = 0x83;
/** Control register 0x0E: countdown disabled. */
constexpr uint8_t TIMER_CONTROL_OFF = 0x03;

/** One program request, resolved the way the driver resolves it. */
struct Selection {
  /** True when the driver chose minute resolution. */
  bool minuteMode;
  /** Count written to register 0x0F, already clamped. */
  uint8_t count;
  /** Seconds the chip would count down before the falling clock ticks. */
  int programmedSeconds;
  /** Value written to control register 0x0E. */
  uint8_t control;
  /** True when the request disables the countdown instead of arming it. */
  bool disabled;
};

/**
 * Resolve a requested delay into the countdown registers.
 *
 * A request of zero or less disables the countdown, which is what the driver
 * does before it programs anything.
 */
constexpr Selection select(int requestedSeconds) {
  if (requestedSeconds <= 0) {
    return {false, 0, 0, TIMER_CONTROL_OFF, true};
  }

  const bool minuteMode = requestedSeconds >= MINUTE_MODE_THRESHOLD_S;
  const int divisor = minuteMode ? MINUTE_S : 1;
  int raw = minuteMode ? ((requestedSeconds + 30) / MINUTE_S) : requestedSeconds;
  if (raw > COUNTER_MAX) {
    raw = COUNTER_MAX;
  }
  const uint8_t count = static_cast<uint8_t>(raw);
  return {minuteMode, count, static_cast<int>(count) * divisor,
          static_cast<uint8_t>(minuteMode ? TIMER_CONTROL_MINUTES : TIMER_CONTROL_SECONDS), false};
}

/**
 * The firmware acceptance rule for an armed countdown.
 *
 * A late wake misses the saved shot deadline, so the driver's rounding up is
 * rejected. An early wake is safe only inside the resume window, because the
 * minute-mode clamp can otherwise wake hours before the deadline. Must stay
 * identical to the guard in `Platform::powerOffUntil()`.
 */
constexpr bool accepted(int requestedSeconds, int programmedSeconds, uint32_t maxEarlySeconds) {
  if (programmedSeconds <= 0) {
    return false;
  }
  const int earlySeconds = requestedSeconds - programmedSeconds;
  return (earlySeconds >= 0) && (earlySeconds <= static_cast<int>(maxEarlySeconds));
}

}  // namespace RtcCountdown
}  // namespace Furble

#endif
