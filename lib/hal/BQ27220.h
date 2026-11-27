#ifndef FURBLE_HAL_BQ27220_H
#define FURBLE_HAL_BQ27220_H

#include <cstdint>

#include "HalBus.h"

/*
 * Texas Instruments BQ27220 fuel gauge, standard commands only.
 *
 * Source: TI "BQ27220 Technical Reference Manual", SLUUBD4A, April 2016,
 * revised November 2022. Each standard command is a two-byte word, least
 * significant byte at the lower command code (section 2.1, Table 2-1). The
 * 7-bit address is fixed at 0x55 (section 5.1). Read-only use needs no unseal
 * and no data memory access, so this driver never writes the gauge.
 *
 * Bus pacing (section 5.3, "I2C command waiting time"): after a packet the
 * gauge needs 66 us of bus-free time before the next one, so read() asks the
 * bus for that gap between its five packets through RegisterBus::delayUs().
 * The firmware adapter must override that hook; its default is a no-op and
 * the gauge then fails silently, answering with stale or corrupt data.
 */

namespace Furble {
namespace HAL {

class BQ27220 {
 public:
  static constexpr uint8_t ADDRESS = 0x55;

  /** TimeToEmpty() value meaning the battery is not discharging (2.11). */
  static constexpr uint16_t NO_ESTIMATE = 0xFFFF;

  /** Section 5.3: 66 us of bus-free time between standard-command packets. */
  static constexpr uint32_t PACKET_GAP_US = 66;

  /**
   * Section 5.3: poll no faster than twice per second.
   *
   * The gauge refreshes the standard-command data once a second, so a faster
   * poll returns the same sample and spends bus time for nothing. read() does
   * not enforce this: it cannot tell a caller's timer from a busy loop, and
   * blocking for the interval inside a battery read would stall the UI. The
   * caller owns the interval, and this constant states it.
   */
  static constexpr uint32_t MIN_POLL_INTERVAL_MS = 500;

  struct Reading {
    uint16_t voltageMv;       // Voltage(), 0x08, section 2.6
    int16_t currentMa;        // Current(), 0x0C, section 2.8, negative when discharging
    uint8_t percent;          // StateOfCharge(), 0x2C, section 2.21
    uint16_t timeToEmptyMin;  // TimeToEmpty(), 0x16, section 2.11
    bool present;             // BatteryStatus() BATTPRES, 0x0A, section 2.7
    bool discharging;         // BatteryStatus() DSG
  };

  explicit BQ27220(RegisterBus &bus) : m_Bus(bus) {}

  /**
   * Read every field: five standard-command packets with the 66 us bus-free
   * gap of section 5.3 between them, so one call keeps the bus about 264 us.
   *
   * Returns false if any transfer fails, or if StateOfCharge() reads above the
   * 0 to 100 range section 2.21, page 25, gives that field. In either case
   * `reading` is left untouched, so a caller keeps its last good sample: an
   * all-ones SOC word from a gauge that is not estimating must not become a
   * full battery.
   *
   * Rate limit: call this at most twice per second (section 5.3, see
   * MIN_POLL_INTERVAL_MS). One call reads each of the five commands once, so
   * a 2 Hz poll reads every command at the gauge's own 1 Hz refresh rate and
   * stays inside the limit.
   */
  bool read(Reading &reading);

 private:
  bool word(uint8_t command, uint16_t &value);

  RegisterBus &m_Bus;
};

}  // namespace HAL
}  // namespace Furble

#endif
