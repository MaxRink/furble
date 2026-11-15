#ifndef FURBLE_BATTERY_STATUS_H
#define FURBLE_BATTERY_STATUS_H

#include <cstdint>

namespace Furble::BatteryStatus {

constexpr int32_t level(bool supported, int32_t value) {
  return supported ? value : -1;
}

constexpr int16_t voltage(bool supported, int32_t value) {
  return supported ? static_cast<int16_t>(value) : -1;
}

constexpr int32_t current(bool supported, int32_t value) {
  return supported ? value : 0;
}

constexpr int16_t vbus(bool readable, int16_t value) {
  return readable ? value : 0;
}

constexpr bool charging(bool supported, bool value) {
  return supported && value;
}

/**
 * Gate each raw reading by its own capability flag. A board with partial
 * battery support, such as level only, reports the other fields as unknown.
 * Caps is Platform::battery_caps_t and Status is Platform::battery_status_t.
 */
template <typename Caps, typename Status>
constexpr Status gate(const Caps &caps, Status raw) {
  raw.level = level(caps.level, raw.level);
  raw.voltage = voltage(caps.voltage, raw.voltage);
  raw.current = current(caps.current, raw.current);
  raw.charging = charging(caps.charging, raw.charging);
  return raw;
}

}  // namespace Furble::BatteryStatus

#endif
