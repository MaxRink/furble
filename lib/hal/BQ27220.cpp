#include "BQ27220.h"

namespace Furble {
namespace HAL {

namespace {
constexpr uint8_t CMD_VOLTAGE = 0x08;
constexpr uint8_t CMD_BATTERY_STATUS = 0x0A;
constexpr uint8_t CMD_CURRENT = 0x0C;
constexpr uint8_t CMD_TIME_TO_EMPTY = 0x16;
constexpr uint8_t CMD_STATE_OF_CHARGE = 0x2C;

// BatteryStatus() low byte, Table 2-6.
constexpr uint16_t STATUS_DSG = 1U << 0;
constexpr uint16_t STATUS_BATTPRES = 1U << 3;
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
  uint16_t voltage = 0;
  uint16_t status = 0;
  uint16_t current = 0;
  uint16_t tte = 0;
  uint16_t soc = 0;
  if (!word(CMD_VOLTAGE, voltage) || !word(CMD_BATTERY_STATUS, status)
      || !word(CMD_CURRENT, current) || !word(CMD_TIME_TO_EMPTY, tte)
      || !word(CMD_STATE_OF_CHARGE, soc)) {
    return false;
  }
  reading.voltageMv = voltage;
  reading.currentMa = static_cast<int16_t>(current);
  reading.percent = static_cast<uint8_t>((soc > 100) ? 100 : soc);
  reading.timeToEmptyMin = tte;
  reading.present = (status & STATUS_BATTPRES) != 0;
  reading.discharging = (status & STATUS_DSG) != 0;
  return true;
}

}  // namespace HAL
}  // namespace Furble
