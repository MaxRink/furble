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
 */

namespace Furble {
namespace HAL {

class BQ27220 {
 public:
  static constexpr uint8_t ADDRESS = 0x55;

  /** TimeToEmpty() value meaning the battery is not discharging (2.11). */
  static constexpr uint16_t NO_ESTIMATE = 0xFFFF;

  struct Reading {
    uint16_t voltageMv;       // Voltage(), 0x08, section 2.6
    int16_t currentMa;        // Current(), 0x0C, section 2.8, negative when discharging
    uint8_t percent;          // StateOfCharge(), 0x2C, section 2.21
    uint16_t timeToEmptyMin;  // TimeToEmpty(), 0x16, section 2.11
    bool present;             // BatteryStatus() BATTPRES, 0x0A, section 2.7
    bool discharging;         // BatteryStatus() DSG
  };

  explicit BQ27220(RegisterBus &bus) : m_Bus(bus) {}

  /** Read every field. Returns false if any transfer fails. */
  bool read(Reading &reading);

 private:
  bool word(uint8_t command, uint16_t &value);

  RegisterBus &m_Bus;
};

}  // namespace HAL
}  // namespace Furble

#endif
