#ifndef FURBLE_PLATFORM_POLICY_H
#define FURBLE_PLATFORM_POLICY_H

#include <cstdint>

namespace Furble {
namespace PlatformPolicy {

/**
 * Minimum interval between M5PM1 button-state reads.
 *
 * The UI loop runs every 5 ms and used to read the PMIC button on every pass,
 * about 200 I2C transactions per second. Twenty milliseconds keeps a 50 Hz
 * sample while cutting the bus traffic tenfold. A human click is far longer
 * than 50 ms, so no edge is lost by the coarser cadence.
 */
constexpr uint32_t PM1_POLL_MS = 20;

/** True when the next PM1 poll is due, wrap-safe for the millisecond tick. */
constexpr bool pm1PollDue(uint32_t now, uint32_t last) {
  return static_cast<uint32_t>(now - last) >= PM1_POLL_MS;
}

/**
 * AXP192 ADC channel masks after M5Unified's all-on default.
 *
 * Furble reads battery voltage/current and VBUS voltage. The ACIN channels,
 * VBUS current, APS voltage, TS pin, temperature, and GPIO channels are unused,
 * so their conversion current is pure waste.
 *   0x82: bit7 battery voltage, bit6 battery current, bit3 VBUS voltage.
 *   0x83: every bit off (temperature and GPIO channels).
 */
constexpr uint8_t AXP192_ADC_ENABLE_1 = 0xC8;
constexpr uint8_t AXP192_ADC_ENABLE_2 = 0x00;

/** True when a 0x82 mask keeps the channels Furble actually reads. */
constexpr bool axp192AdcMaskKeepsRequired(uint8_t mask) {
  constexpr uint8_t BATTERY_VOLTAGE = 0x80;
  constexpr uint8_t BATTERY_CURRENT = 0x40;
  constexpr uint8_t VBUS_VOLTAGE = 0x08;
  return (mask & (BATTERY_VOLTAGE | BATTERY_CURRENT | VBUS_VOLTAGE))
         == (BATTERY_VOLTAGE | BATTERY_CURRENT | VBUS_VOLTAGE);
}

}  // namespace PlatformPolicy
}  // namespace Furble

#endif  // FURBLE_PLATFORM_POLICY_H
