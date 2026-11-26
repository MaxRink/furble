// The StickS3 speaker notice: at or below 75 percent volume on battery, full
// volume when VBUS is present. The ceiling lives in
// include/FurbleFeedbackPolicy.h so the production decision is testable, and
// the stored setting is never rewritten by it.

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "FurbleFeedbackPolicy.h"

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using Furble::FeedbackPolicy::effectiveVolume;
  using Furble::FeedbackPolicy::MAX_BATTERY_VOLUME;
  using Furble::FeedbackPolicy::onBatteryPower;

  check(MAX_BATTERY_VOLUME == 191, "75 percent of the 0 to 255 range is 191");

  // On battery: a setting above the ceiling is capped, one at or below it is
  // passed through untouched.
  check(effectiveVolume(192, true) == 191, "192 is capped to 191 on battery");
  check(effectiveVolume(255, true) == 191, "full volume is capped to 191 on battery");
  check(effectiveVolume(MAX_BATTERY_VOLUME, true) == MAX_BATTERY_VOLUME,
        "a setting at the ceiling is unchanged on battery");
  check(effectiveVolume(180, true) == 180, "a setting below the ceiling is unchanged on battery");
  check(effectiveVolume(0, true) == 0, "a muted setting stays muted on battery");

  // On VBUS: the configured volume reaches the amplifier unchanged.
  check(effectiveVolume(192, false) == 192, "192 is unchanged on VBUS");
  check(effectiveVolume(255, false) == 255, "full volume is unchanged on VBUS");
  check(effectiveVolume(0, false) == 0, "a muted setting stays muted on VBUS");

  // Power-source decision. Mains power requires a positive VBUS reading; the
  // charge flag is deliberately not an input, because a failed PMIC read
  // reports "charging" on the StickS3 (the charge-status GPIO input returns
  // false when the I2C transaction fails, and isCharging() maps false to
  // is_charging). A zero reading therefore decides "battery", the safe side for
  // the amplifier.
  check(onBatteryPower(true, false), "no VBUS reading is battery power");
  check(!onBatteryPower(true, true), "a positive VBUS reading is mains power");
  check(!onBatteryPower(false, false),
        "a board with no VBUS or charge telemetry cannot claim battery power");

  // The two states the device actually reports, end to end through the policy.
  const bool on_battery = onBatteryPower(true, false);
  const bool on_vbus = onBatteryPower(true, true);
  check(effectiveVolume(255, on_battery) == 191, "stored 255 plays at 191 on battery");
  check(effectiveVolume(255, on_vbus) == 255, "stored 255 plays at 255 on VBUS");

  std::cout << "feedback-volume-policy: PASS\n";
  return 0;
}
