#ifndef FURBLE_POWER_BACKEND_H
#define FURBLE_POWER_BACKEND_H

#include <cstdint>
#include <type_traits>

namespace Furble {

/**
 * What a board's power hardware must provide.
 *
 * This is a link-time contract, not a virtual interface. Exactly one
 * `Platform` is linked into each image: src/FurblePlatform.cpp for the M5
 * boards (M5PM1, AXP192, AXP2101 through M5Unified, IP5306, ADC) and
 * sim/FurblePlatformSim.cpp for the simulator. `Platform` derives from this
 * class for the shared types and defines every member that
 * meetsPowerBackend() checks. There is no vtable on purpose: a vtable keeps
 * every power path alive, and on the headless Waveshare image that pulled the
 * ESP-IDF sleep code into IRAM past its budget (plan 322). A board family the
 * M5 code does not cover supplies its own `Platform` implementation file,
 * selected by its CMake profile, built on the drivers in lib/hal.
 */
class PowerBackend {
 public:
  /** Battery measurements available on this board. */
  typedef struct {
    bool level;
    bool voltage;
    bool current;
    bool charging;
  } battery_caps_t;

  /**
   * Battery measurement sample.
   *
   * Only fields flagged in battery_caps_t are meaningful.
   */
  typedef struct {
    uint8_t level;     // percent
    uint16_t voltage;  // millivolts
    int32_t current;   // milliamps, positive when charging
    bool charging;
  } battery_t;

  /**
   * Switchable supplies, named by what they feed.
   *
   * `EXT_5V` is the Grove 5 V / external 5 V output: M5PM1 PWR_CFG bit 3 on the
   * StickS3 (the EXT_5V "5VINOUT" port, which also powers the infrared
   * circuit), AXP192 EXTEN on the StickC family, AXP2101 LDOIO0 on the Core2
   * and Tough. It is not a GPS-only rail; see include/FurbleRail.h for the
   * holders that keep it up and plans/364-ext5v-rail.md for the ownership
   * model.
   */
  enum class Rail : uint8_t {
    EXT_5V,
  };
};

/**
 * True when P has every PowerBackend member with the documented signature.
 *
 *  - battery_caps_t getBatteryCaps(void)
 *  - battery_t readBattery(void): unsupported measurements read as zero
 *  - uint16_t getBatteryCapacity(void): mAh, zero if unknown
 *  - bool setRail(Rail rail, bool on): false when the board has no switch, the
 *    rail does not read back in the requested state, or the output-mode
 *    interlock refused the enable. The request is advisory: the PMIC may
 *    refuse an enable. Rail::EXT_5V names the external 5 V output; on the
 *    Core2 and Tough that is one BUS_5V net shared by Grove Port A and the
 *    M-Bus.
 *  - uint8_t getPWRClickCount(void): PMIC key clicks since the last call
 *  - bool powerOff(void): never returns on success
 *  - bool canTimedWake(void)
 *  - bool powerOffUntil(uint32_t seconds)
 *  - bool consumeTimedWake(void)
 *
 * A missing member fails to compile; a different signature makes this false.
 */
template <typename P>
constexpr bool meetsPowerBackend(void) {
  using B = PowerBackend;
  return std::is_base_of<B, P>::value
         && std::is_same<decltype(&P::getBatteryCaps), B::battery_caps_t (P::*)(void)>::value
         && std::is_same<decltype(&P::readBattery), B::battery_t (P::*)(void)>::value
         && std::is_same<decltype(&P::getBatteryCapacity), uint16_t (P::*)(void)>::value
         && std::is_same<decltype(&P::setRail), bool (P::*)(B::Rail, bool)>::value
         && std::is_same<decltype(&P::getPWRClickCount), uint8_t (P::*)(void)>::value
         && std::is_same<decltype(&P::powerOff), bool (P::*)(void)>::value
         && std::is_same<decltype(&P::canTimedWake), bool (P::*)(void)>::value
         && std::is_same<decltype(&P::powerOffUntil), bool (P::*)(uint32_t)>::value
         && std::is_same<decltype(&P::consumeTimedWake), bool (P::*)(void)>::value
         && !std::is_polymorphic<P>::value;
}

}  // namespace Furble

#endif
