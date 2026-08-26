#include <iostream>

#include "FurbleBatteryStatus.h"

int main() {
  int failures = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      failures++;
    }
  };

  // The Waveshare ESP32-S3-ETH has no battery or PMIC. Unsupported readings
  // must never expose M5Unified fallback values to the companion or console.
  check(Furble::BatteryStatus::level(false, 83) == -1, "unsupported battery level is unknown");
  check(Furble::BatteryStatus::voltage(false, 4095) == -1,
        "unsupported battery voltage is unknown");
  check(Furble::BatteryStatus::current(false, -123) == 0, "unsupported battery current is zero");
  check(Furble::BatteryStatus::vbus(false, 5000) == 0,
        "unreadable external-power telemetry is zero");
  check(!Furble::BatteryStatus::charging(false, true), "unsupported charging state is false");

  check(Furble::BatteryStatus::level(true, 83) == 83, "supported battery level is preserved");
  check(Furble::BatteryStatus::voltage(true, 4095) == 4095,
        "supported battery voltage is preserved");
  check(Furble::BatteryStatus::current(true, -123) == -123,
        "supported battery current is preserved");
  check(Furble::BatteryStatus::vbus(true, 5000) == 5000,
        "readable external-power telemetry is preserved");
  check(Furble::BatteryStatus::charging(true, true), "supported charging state is preserved");

  return failures == 0 ? 0 : 1;
}
