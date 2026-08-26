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

}  // namespace Furble::BatteryStatus

#endif
