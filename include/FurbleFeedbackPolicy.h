#ifndef FURBLE_FEEDBACK_POLICY_H
#define FURBLE_FEEDBACK_POLICY_H

#include <cstdint>

namespace Furble {
namespace FeedbackPolicy {

/**
 * Volume ceiling while the device runs from the battery.
 *
 * The M5Stack StickS3 documentation carries a Speaker Volume Notice: keep the
 * speaker volume at or below 75 percent when the device runs from the battery,
 * because the speaker amplifier's current can brown the supply out and reboot
 * the device. 191 is 75 percent of the 0 to 255 volume range, rounded down.
 * The ceiling is a supply limit, not a comfort setting: the stored volume is
 * never rewritten, only what reaches the amplifier is capped.
 */
constexpr uint8_t MAX_BATTERY_VOLUME = 191;

/**
 * True when the board is running from the battery alone.
 *
 * `telemetry` is false for a board that can report neither VBUS nor a charge
 * state; such a board cannot tell its power source apart, so its configured
 * volume stands rather than being capped on a guess.
 *
 * `vbus_present` must come from a positive VBUS reading, never from the charge
 * flag. M5Unified's StickS3 isCharging() reads the PMIC charge-status GPIO
 * input, which returns false when the I2C transaction fails, and maps false to
 * is_charging, so a failed read reports "charging". Only a positive VBUS
 * measurement is evidence of mains power. A full battery still on the cable
 * keeps VBUS asserted, so it still counts as mains. A zero reading is treated
 * as battery power, which is the safe direction for the amplifier.
 */
constexpr bool onBatteryPower(bool telemetry, bool vbus_present) {
  return telemetry && !vbus_present;
}

/** The volume to hand the amplifier for a configured setting. */
constexpr uint8_t effectiveVolume(uint8_t configured, bool on_battery) {
  return (on_battery && (configured > MAX_BATTERY_VOLUME)) ? MAX_BATTERY_VOLUME : configured;
}

}  // namespace FeedbackPolicy
}  // namespace Furble

#endif  // FURBLE_FEEDBACK_POLICY_H
