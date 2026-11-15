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

  // Each field follows its own capability flag, never the union of them.
  struct caps_t {
    bool level, voltage, current, charging;
  };
  struct status_t {
    int32_t level;
    int16_t voltage;
    int32_t current;
    int16_t vbus;
    bool charging;
  };
  constexpr status_t raw = {83, 4012, -123, 5000, true};

  // M5StickS3: M5PM1 has no battery current backend.
  const status_t stickS3 = Furble::BatteryStatus::gate(caps_t {true, true, false, true}, raw);
  check(stickS3.level == 83 && stickS3.voltage == 4012 && stickS3.charging,
        "StickS3 level, voltage and charging are preserved");
  check(stickS3.current == 0, "StickS3 current is unsupported");

  // M5Stack: IP5306 reports a coarse level only.
  const status_t core = Furble::BatteryStatus::gate(caps_t {true, false, false, false}, raw);
  check(core.level == 83, "M5Stack level is preserved");
  check(core.voltage == -1 && core.current == 0 && !core.charging,
        "M5Stack voltage, current and charging are unsupported");
  check(core.vbus == 5000, "gating leaves VBUS to its own sense flag");

  const status_t none = Furble::BatteryStatus::gate(caps_t {false, false, false, false}, raw);
  check(none.level == -1 && none.voltage == -1 && none.current == 0 && !none.charging,
        "a board without a battery reports every field as unknown");

  return failures == 0 ? 0 : 1;
}
