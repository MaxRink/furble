#ifndef FURBLE_PLATFORM_POLICY_H
#define FURBLE_PLATFORM_POLICY_H

#include <cstdint>

namespace Furble {
namespace PlatformPolicy {

/**
 * Minimum interval between M5PM1 button-state reads.
 *
 * The UI loop runs every 5 ms with the panel on and used to read the PMIC
 * button on every pass, about 200 I2C transactions per second. Twenty
 * milliseconds keeps a 50 Hz sample while cutting the bus traffic tenfold. A
 * human click is far longer than 50 ms, so no edge is lost by the coarser
 * cadence. With the panel off the loop pass is at most this long, so a wake
 * press is still sampled once per pass.
 */
constexpr uint32_t PM1_POLL_MS = 20;

/** True when the next PM1 poll is due, wrap-safe for the millisecond tick. */
constexpr bool pm1PollDue(uint32_t now, uint32_t last) {
  return static_cast<uint32_t>(now - last) >= PM1_POLL_MS;
}

/**
 * AXP192 ADC channel masks after M5Unified's all-on default.
 *
 * Furble reads battery voltage and current, and VBUS voltage. The VBUS current
 * channel (0x82 bit 2) is read by M5Unified's legacy AXP192 setExtOutput() path
 * on the Core2 and Tough only, where it refuses external power when the battery
 * is low and no ACIN supply is present (pinned 0.2.20 Power_Class.cpp:951-981,
 * the `board_M5StackCore2`/`board_M5Tough` case). The StickC and StickC Plus
 * case only calls Axp192.setEXTEN() and the Plus2 falls through to the empty
 * default, so neither reads VBUS current and furble never does.
 *
 * One mask is written on every AXP192 board, so bit2 stays set to keep the
 * Core2/Tough path unchanged. Trimming it only on the StickC and Plus would
 * change that board family's ADC enable map, which plan 350 records as a
 * follow-up: the AXP192 register details could not be verified in that pass, so
 * everything beyond the `REG 82H` and `REG 83H` rows in
 * docs/power/datasheet-register.md is UNVERIFIED here.
 *   0x82: bit7 battery voltage, bit6 battery current, bit3 VBUS voltage,
 *         bit2 VBUS current.
 *   0x83: every bit off (temperature and GPIO channels).
 */
constexpr uint8_t AXP192_ADC_ENABLE_1 = 0xCC;
constexpr uint8_t AXP192_ADC_ENABLE_2 = 0x00;

/** True when a 0x82 mask keeps every channel this build depends on. */
constexpr bool axp192AdcMaskKeepsRequired(uint8_t mask) {
  constexpr uint8_t BATTERY_VOLTAGE = 0x80;
  constexpr uint8_t BATTERY_CURRENT = 0x40;
  constexpr uint8_t VBUS_VOLTAGE = 0x08;
  constexpr uint8_t VBUS_CURRENT = 0x04;
  return (mask & (BATTERY_VOLTAGE | BATTERY_CURRENT | VBUS_VOLTAGE | VBUS_CURRENT))
         == (BATTERY_VOLTAGE | BATTERY_CURRENT | VBUS_VOLTAGE | VBUS_CURRENT);
}

/** What `Platform::powerOff()` does with a request. */
enum class PowerOffAction : uint8_t {
  CUT,     // the profile has a power-off path, attempt it
  REFUSE,  // no path: log and report failure so the caller keeps running
};

/**
 * Decide a power-off request from the board profile alone.
 *
 * A board with no PMIC, no battery and no switchable rail has nothing for a
 * software power off to switch. M5Unified's `Power_Class::powerOff()` arms no
 * wake source on such a board and ends at `esp_deep_sleep_start()`, so the node
 * stays asleep until a power cycle while the call reports success. Claiming
 * success there is worse than refusing: the caller stops tearing down and stays
 * alive, and the log names the refused profile.
 *
 * Only the row's `powerOffPath` column is an input, and the Waveshare
 * ESP32-S3-ETH row is the only one that reports false. The `canPowerOff` column
 * is the UI policy gate for the automatic policies and is false on the Core,
 * which keeps its IP5306 path. The generic unknown-board row keeps attempting,
 * as it did before.
 */
constexpr PowerOffAction powerOffAction(bool hasPath) {
  return hasPath ? PowerOffAction::CUT : PowerOffAction::REFUSE;
}

/**
 * The supply the M5PM1 reports for the board, from PWR_SRC 0x04 bits 2:0.
 *
 * The register is M5PM1_REG_PWR_SRC 0x04. 0x07 is HOLD_CFG, the power-hold
 * configuration, and reading it would answer a different question entirely.
 *
 * The numbering follows the vendor enumeration (M5PM1 1.0.6, src/M5PM1.h
 * m5pm1_pwr_src_t), so a mapping error is a static_assert in
 * src/FurblePlatform.cpp rather than a silent reinterpretation.
 */
enum class RailSupply : uint8_t {
  USB_5V = 0,   // USB/DC 5 V input
  PORT_5V = 1,  // an external 5 V source driving the bidirectional 5 V port
  BATTERY = 2,  // battery only
  UNKNOWN = 3,  // no supply reported
};

/** Why the EXT_5V output-mode interlock allowed or refused a request. */
enum class RailEnableVerdict : uint8_t {
  ALLOW,
  REFUSE_EXTERNAL_SOURCE,
  REFUSE_UNKNOWN_SUPPLY,
};

/**
 * Interlock for committing the EXT_5V port to output mode.
 *
 * The M5Stack documentation for EXT_5V warns that the port defaults to INPUT
 * and that in output mode power may only be applied through USB or 5VIN,
 * otherwise there is a risk of short circuit and device damage (quoted in
 * plans/364-ext5v-rail.md and docs/power/board-power.md). The port cannot be
 * sensed on this board, so firmware refuses the enable while an external 5 V
 * source is present at the port (PWR_SRC reports the bidirectional port
 * powered) or while the supply state cannot be established at all. The failure
 * direction is closed: a refusal leaves PWR_CFG 0x06 untouched, so EXT_5V keeps
 * the vendor default of INPUT, and the caller's read-back reports false.
 *
 * Switching the port back to input mode is always allowed; it is the vendor
 * default and the safe direction.
 *
 * This is an allowlist: only the two supplies the vendor documents as a source
 * the board may drive the port from, USB 5 V and the battery, allow output
 * mode. Every other value refuses, the undefined 4 to 7 that PWR_SRC bits 2:0
 * can report included, because a supply this firmware cannot name is one it
 * cannot rule out.
 */
constexpr RailEnableVerdict railEnableVerdict(bool supplyKnown, RailSupply supply) {
  if (!supplyKnown) {
    return RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY;
  }

  switch (supply) {
    case RailSupply::USB_5V:
    case RailSupply::BATTERY:
      return RailEnableVerdict::ALLOW;
    case RailSupply::PORT_5V:
      return RailEnableVerdict::REFUSE_EXTERNAL_SOURCE;
    case RailSupply::UNKNOWN:
      return RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY;
  }

  // A value outside the enumeration (PWR_SRC bits 2:0 report 0 to 7) refuses
  // rather than falling through to ALLOW.
  return RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY;
}

/** Log text and simulator query text for an interlock verdict. */
constexpr const char *railEnableVerdictName(RailEnableVerdict verdict) {
  switch (verdict) {
    case RailEnableVerdict::ALLOW:
      return "allow";
    case RailEnableVerdict::REFUSE_EXTERNAL_SOURCE:
      return "refuse-external-source";
    case RailEnableVerdict::REFUSE_UNKNOWN_SUPPLY:
      return "refuse-unknown-supply";
  }
  return "refuse-unknown-supply";
}

}  // namespace PlatformPolicy
}  // namespace Furble

#endif  // FURBLE_PLATFORM_POLICY_H
