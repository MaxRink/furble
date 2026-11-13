#ifndef FURBLE_POWER_BACKEND_H
#define FURBLE_POWER_BACKEND_H

#include <cstdint>

namespace Furble {

/**
 * What a board's power hardware must provide.
 *
 * Exactly one implementation is linked into each image: `Platform` in
 * src/FurblePlatform.cpp for the M5 boards (M5PM1, AXP192, AXP2101 through
 * M5Unified, IP5306, ADC), and `Platform` in sim/FurblePlatformSim.cpp for the
 * simulator. `Platform` is final, so calls through it compile to direct calls.
 * A board family the M5 code does not cover supplies its own implementation
 * file, selected by its CMake profile, built on the drivers in lib/hal. See
 * plans/322-hardware-abstraction.md.
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

  virtual ~PowerBackend() = default;

  virtual battery_caps_t getBatteryCaps(void) = 0;

  /** Read the battery, unsupported measurements are returned as zero. */
  virtual battery_t readBattery(void) = 0;

  virtual BatterySource getBatterySource(void) = 0;

  /** Battery capacity in mAh, zero if unknown. */
  virtual uint16_t getBatteryCapacity(void) = 0;

  /**
   * Switch a named rail.
   *
   * @return false when the board has no switch for that rail.
   */
  virtual bool setRail(Rail rail, bool on) = 0;

  /** Power-button clicks from the PMIC since the last call. */
  virtual uint8_t getPWRClickCount(void) = 0;

  /** Power off. Never returns on success. */
  virtual bool powerOff(void) = 0;

  virtual bool canTimedWake(void) = 0;

  /** Power off and arrange a timed power-on. */
  virtual bool powerOffUntil(uint32_t seconds) = 0;

  /** Consume the marker left by a timed power-on. */
  virtual bool consumeTimedWake(void) = 0;
};

}  // namespace Furble

#endif
