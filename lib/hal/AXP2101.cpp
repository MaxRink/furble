#include "AXP2101.h"

namespace Furble {
namespace HAL {

namespace {
// Register addresses, X-Powers "AXP2101 Single Cell NVDC PMU with E-gauge"
// datasheet revision 1.0, 2021-05-31, section 6.13.2 Register Description.
// AXP2101.h cites the subsection for most registers; REG 03 is absent from the
// revision 1.0 map and REG 41 carries no subsection there. Both are recorded,
// with the rest of the open questions, in docs/components/axp2101.md.
constexpr uint8_t REG_STATUS1 = 0x00;
constexpr uint8_t REG_STATUS2 = 0x01;
constexpr uint8_t REG_CHIP_ID = 0x03;
constexpr uint8_t REG_COMMON = 0x10;
constexpr uint8_t REG_MODULE_EN = 0x18;
constexpr uint8_t REG_VBAT_H = 0x34;
constexpr uint8_t REG_IRQ_EN1 = 0x41;
constexpr uint8_t REG_IRQ_STATUS1 = 0x49;
constexpr uint8_t REG_BACKUP_VOLTAGE = 0x6A;
constexpr uint8_t REG_DCDC_EN = 0x80;
constexpr uint8_t REG_LDO_EN0 = 0x90;
constexpr uint8_t REG_LDO_EN1 = 0x91;
constexpr uint8_t REG_ALDO1_VOLTAGE = 0x92;
constexpr uint8_t REG_PERCENT = 0xA4;

// INFERRED, not from a datasheet: neither AXP2101 revision the driver cites
// defines register 0x03, revision 0.1 documents 0x47 or 0x57 for it, and 0x4A
// traces to the X-Powers reference library this value is copied from. It is
// also what M5Unified's AXP2101 path compares against (M5Unified's own AXP2101 path,
// src/utility/power/AXP2101_Class.cpp:34). Recorded as inferred in
// docs/components/axp2101.md row 17 and open question C-19a, and the probe
// retries because a failed identity read costs every rail and the power-off
// path.
constexpr uint8_t CHIP_ID = 0x4A;
constexpr uint8_t STATUS1_VBUS_GOOD = 1U << 5;
constexpr uint8_t STATUS1_BATTERY_PRESENT = 1U << 3;
constexpr uint8_t MODULE_GAUGE = 1U << 3;
constexpr uint8_t MODULE_BACKUP_CHARGE = 1U << 2;
constexpr uint8_t COMMON_SOFT_OFF = 1U << 0;
// REG 41 and REG 49 use one bit assignment for the power key: bit 3 short
// press, bit 2 long press, bit 1 falling edge, bit 0 rising edge (sections
// 6.13.2.41 and 6.13.2.44). One mask enables and decodes the same four events.
constexpr uint8_t IRQ1_RISING = 1U << 0;
constexpr uint8_t IRQ1_FALLING = 1U << 1;
constexpr uint8_t IRQ1_LONG = 1U << 2;
constexpr uint8_t IRQ1_SHORT = 1U << 3;
constexpr uint8_t IRQ1_KEY_MASK = IRQ1_SHORT | IRQ1_LONG | IRQ1_FALLING | IRQ1_RISING;

struct RailBit {
  uint8_t reg;
  uint8_t bit;
};

// Indexed by Rail. Section 6.13.2.75 labels REG 90 bit 4 "aldo1 enable" a
// second time; it is BLDO1, the only LDO left, and the voltage registers
// (92-97) follow the same ALDO1-4, BLDO1, BLDO2 order.
constexpr RailBit RAILS[AXP2101::RAIL_COUNT] = {
    {REG_DCDC_EN, 1U << 0},
    {REG_DCDC_EN, 1U << 1},
    {REG_DCDC_EN, 1U << 2},
    {REG_DCDC_EN, 1U << 3},
    {REG_DCDC_EN, 1U << 4},
    {REG_LDO_EN0, 1U << 0},
    {REG_LDO_EN0, 1U << 1},
    {REG_LDO_EN0, 1U << 2},
    {REG_LDO_EN0, 1U << 3},
    {REG_LDO_EN0, 1U << 4},
    {REG_LDO_EN0, 1U << 5},
    {REG_LDO_EN0, 1U << 6},
    {REG_LDO_EN0, 1U << 7},
    {REG_LDO_EN1, 1U << 0},
};

bool validRail(AXP2101::Rail rail) {
  return static_cast<uint8_t>(rail) < AXP2101::RAIL_COUNT;
}
}  // namespace

bool AXP2101::probe(void) {
  uint8_t id = 0;
  return m_Bus.read8(REG_CHIP_ID, id) && (id == CHIP_ID);
}

bool AXP2101::batteryPresent(bool &present) {
  uint8_t status = 0;
  if (!m_Bus.read8(REG_STATUS1, status)) {
    return false;
  }
  present = (status & STATUS1_BATTERY_PRESENT) != 0;
  return true;
}

bool AXP2101::vbusGood(bool &good) {
  uint8_t status = 0;
  if (!m_Bus.read8(REG_STATUS1, status)) {
    return false;
  }
  good = (status & STATUS1_VBUS_GOOD) != 0;
  return true;
}

bool AXP2101::batteryVoltage(uint16_t &millivolts) {
  uint8_t raw[2] = {};
  if (!m_Bus.read(REG_VBAT_H, raw, sizeof(raw))) {
    return false;
  }
  // Bits 7:6 of REG 34 are the ADC debug channel select, not data.
  millivolts = static_cast<uint16_t>(((raw[0] & 0x3FU) << 8) | raw[1]);
  return true;
}

bool AXP2101::batteryPercent(uint8_t &percent) {
  bool present = false;
  uint8_t module = 0;
  uint8_t value = 0;
  if (!batteryPresent(present) || !present || !m_Bus.read8(REG_MODULE_EN, module)
      || ((module & MODULE_GAUGE) == 0) || !m_Bus.read8(REG_PERCENT, value)) {
    return false;
  }
  percent = (value > 100) ? 100 : value;
  return true;
}

bool AXP2101::chargeState(Charge &state) {
  uint8_t status = 0;
  if (!m_Bus.read8(REG_STATUS2, status)) {
    return false;
  }
  switch ((status >> 5) & 0x03U) {
    case 0x01:
      state = Charge::CHARGING;
      return true;
    case 0x02:
      state = Charge::DISCHARGING;
      return true;
    case 0x00:
      state = Charge::STANDBY;
      return true;
    default:
      // 11b is reserved
      return false;
  }
}

bool AXP2101::setRail(Rail rail, bool on) {
  if (!validRail(rail)) {
    return false;
  }
  if (!on && ((m_ProtectedRails & railBit(rail)) != 0)) {
    return false;
  }
  const RailBit &r = RAILS[static_cast<uint8_t>(rail)];
  return m_Bus.update8(r.reg, r.bit, on ? r.bit : 0);
}

bool AXP2101::railEnabled(Rail rail, bool &on) {
  if (!validRail(rail)) {
    return false;
  }
  const RailBit &r = RAILS[static_cast<uint8_t>(rail)];
  uint8_t value = 0;
  if (!m_Bus.read8(r.reg, value)) {
    return false;
  }
  on = (value & r.bit) != 0;
  return true;
}

bool AXP2101::setLdoVoltage(Rail rail, uint16_t millivolts) {
  const uint8_t index = static_cast<uint8_t>(rail);
  const uint8_t first = static_cast<uint8_t>(Rail::ALDO1);
  const uint8_t last = static_cast<uint8_t>(Rail::BLDO2);
  if ((index < first) || (index > last) || (millivolts < 500) || (millivolts > 3500)) {
    return false;
  }
  // 00000b is 0.5 V and 11110b is 3.5 V; 11111b is reserved and unreachable.
  const uint8_t code = static_cast<uint8_t>((millivolts - 500) / 100);
  return m_Bus.update8(static_cast<uint8_t>(REG_ALDO1_VOLTAGE + (index - first)), 0x1F, code);
}

bool AXP2101::setBackupCharge(bool enable, uint16_t millivolts) {
  if (enable) {
    if ((millivolts < 2600) || (millivolts > 3300)) {
      return false;
    }
    const uint8_t code = static_cast<uint8_t>((millivolts - 2600) / 100);
    if (!m_Bus.update8(REG_BACKUP_VOLTAGE, 0x07, code)) {
      return false;
    }
  }
  return m_Bus.update8(REG_MODULE_EN, MODULE_BACKUP_CHARGE, enable ? MODULE_BACKUP_CHARGE : 0);
}

bool AXP2101::enablePowerKeyIrq(bool enable) {
  // Every key bit the driver decodes, edges included: section 6.12.1 sets a
  // status bit only when its enable bit is set, and section 6.13.2.41 leaves
  // bits 1:0 at 0, so an enable of bits 3:2 alone makes PowerKey::falling and
  // PowerKey::rising undeliverable. See the header for the interrupt cost.
  return m_Bus.update8(REG_IRQ_EN1, IRQ1_KEY_MASK, enable ? IRQ1_KEY_MASK : 0);
}

bool AXP2101::readPowerKey(PowerKey &key) {
  uint8_t status = 0;
  if (!m_Bus.read8(REG_IRQ_STATUS1, status)) {
    return false;
  }
  key = {(status & IRQ1_SHORT) != 0, (status & IRQ1_LONG) != 0, (status & IRQ1_FALLING) != 0,
         (status & IRQ1_RISING) != 0};
  // Write 1 to clear only the key bits, never the VBUS or battery events a
  // different consumer may still need.
  const uint8_t clear = status & IRQ1_KEY_MASK;
  return (clear == 0) || m_Bus.write8(REG_IRQ_STATUS1, clear);
}

bool AXP2101::powerOff(void) {
  return m_Bus.update8(REG_COMMON, COMMON_SOFT_OFF, COMMON_SOFT_OFF);
}

}  // namespace HAL
}  // namespace Furble
