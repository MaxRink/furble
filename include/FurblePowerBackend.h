#ifndef FURBLE_POWER_BACKEND_H
#define FURBLE_POWER_BACKEND_H

#include <cstdint>

namespace Furble {

/**
 * What a board's power hardware must provide.
 *
 * This is a link-time contract, not a virtual interface. Exactly one
 * `Platform` is linked into each image: src/FurblePlatform.cpp for the M5
 * boards (M5PM1, AXP192, AXP2101 through M5Unified, IP5306, ADC) and
 * sim/FurblePlatformSim.cpp for the simulator. `Platform` derives from this
 * class for the shared types and defines every member listed below. There is
 * no vtable on purpose: a vtable keeps every power path alive, and on the
 * headless Waveshare image that pulled the ESP-IDF sleep code into IRAM past
 * its budget (plan 322). A board family the M5 code does not cover supplies
 * its own `Platform` implementation file, selected by its CMake profile, built
 * on the drivers in lib/hal.
 *
 * Required members of `Platform`:
 *  - battery_caps_t getBatteryCaps(void)
 *  - battery_t readBattery(void): unsupported measurements read as zero
 *  - BatterySource getBatterySource(void)
 *  - uint16_t getBatteryCapacity(void): mAh, zero if unknown
 *  - bool setRail(Rail rail, bool on): false when the board has no switch
 *  - uint8_t getPWRClickCount(void): PMIC key clicks since the last call
 *  - bool powerOff(void): never returns on success
 *  - bool canTimedWake(void)
 *  - bool powerOffUntil(uint32_t seconds)
 *  - bool consumeTimedWake(void)
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

  /** Where the battery percentage comes from. */
  enum class BatterySource : uint8_t {
    NONE,      // no battery telemetry
    GAUGE,     // a fuel gauge (AXP2101 E-gauge, BQ27220)
    ESTIMATE,  // a voltage curve (AXP192, M5PM1, ADC divider)
    COARSE,    // a few fixed steps (IP5306)
  };

  /** Switchable supplies, named by what they feed. */
  enum class Rail : uint8_t {
    GPS,
    DISPLAY,
    PERIPHERAL,
  };
};

}  // namespace Furble

#endif
