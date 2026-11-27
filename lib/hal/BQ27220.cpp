#include "BQ27220.h"

#include <cstddef>

namespace Furble {
namespace HAL {

namespace {
// Standard commands, TI "BQ27220 Technical Reference Manual" SLUUBD4A (April
// 2016, revised November 2022), section 2.1 Standard Commands, Table 2-1. The
// per-command sections below were checked against the same revision:
// Voltage section 2.6, BatteryStatus section 2.7, Current section 2.8,
// TimeToEmpty section 2.11, StateOfCharge section 2.21.
constexpr uint8_t CMD_VOLTAGE = 0x08;
constexpr uint8_t CMD_BATTERY_STATUS = 0x0A;
constexpr uint8_t CMD_CURRENT = 0x0C;
constexpr uint8_t CMD_TIME_TO_EMPTY = 0x16;
constexpr uint8_t CMD_STATE_OF_CHARGE = 0x2C;

// BatteryStatus() low byte, Table 2-6.
constexpr uint16_t STATUS_DSG = 1U << 0;
constexpr uint16_t STATUS_BATTPRES = 1U << 3;

// The five packets read() issues, in order. A table so the pacing loop below
// has one place to guard: section 5.3 requires the gap between every pair of
// packets, not only between the first two.
constexpr uint8_t READING_COMMANDS[] = {CMD_VOLTAGE, CMD_BATTERY_STATUS, CMD_CURRENT,
                                        CMD_TIME_TO_EMPTY, CMD_STATE_OF_CHARGE};
constexpr size_t READING_COMMAND_COUNT = sizeof(READING_COMMANDS) / sizeof(READING_COMMANDS[0]);
}  // namespace

bool BQ27220::word(uint8_t command, uint16_t &value) {
  uint8_t raw[2] = {};
  if (!m_Bus.read(command, raw, sizeof(raw))) {
    return false;
  }
  value = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
  return true;
}

bool BQ27220::read(Reading &reading) {
  uint16_t words[READING_COMMAND_COUNT] = {};
  for (size_t i = 0; i < READING_COMMAND_COUNT; i++) {
    if (i != 0) {
      // Section 5.3: keep the bus idle 66 us between packets. Asked for before
      // every packet after the first, so the stop condition of the previous
      // packet starts the window.
      m_Bus.delayUs(PACKET_GAP_US);
    }
    if (!word(READING_COMMANDS[i], words[i])) {
      return false;
    }
  }

  Reading result = {};
  result.voltageMv = words[0];
  result.present = (words[1] & STATUS_BATTPRES) != 0;
  result.discharging = (words[1] & STATUS_DSG) != 0;
  result.currentMa = static_cast<int16_t>(words[2]);
  result.timeToEmptyMin = words[3];
  // Section 2.21, page 25: StateOfCharge() is a percentage "with a range of 0
  // to 100". The manual gives this field no out-of-range sentinel, unlike
  // TimeToEmpty() (section 2.11, page 23, 0xFFFF means not discharging), so a
  // word above 100 is not an SOC and the call reports no reading rather than a
  // full battery: an all-ones word from a gauge that is not estimating is the
  // field case this refuses, and 100 percent there is the one answer that
  // hides it.
  const uint16_t soc = words[4];
  if (soc > 100) {
    return false;
  }
  result.percent = static_cast<uint8_t>(soc);
  reading = result;
  return true;
}

}  // namespace HAL
}  // namespace Furble
