// Register-level coverage for the pure chip drivers in lib/hal.
//
// Every expected register, bit and command byte below is taken from the
// datasheet cited in the driver header, not from the driver, so a typo in a
// driver constant fails here:
//
//   AXP2101  X-Powers AXP2101 datasheet revision 1.0, section 6.13.2
//   BQ27220  TI SLUUBD4A, sections 2.1 to 2.21 and Table 2-6
//   XL9555   XINLUDA XL9535/XL9555 datasheet Rev 2.3, section 9.2
//   SX1262   Semtech DS.SX1261-2.W.APP Rev 2.2, sections 8.1, 9.3, 13.1.1

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "AXP2101.h"
#include "BQ27220.h"
#include "SX1262.h"
#include "XL9555.h"

using Furble::HAL::AXP2101;
using Furble::HAL::BQ27220;
using Furble::HAL::RegisterBus;
using Furble::HAL::SX1262;
using Furble::HAL::XL9555;

namespace {

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

/** Register file with a write log. Multi-byte reads walk up the addresses. */
class FakeBus final: public RegisterBus {
 public:
  std::map<uint8_t, uint8_t> regs;
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  bool fail = false;

  bool read(uint8_t reg, uint8_t *data, size_t length) override {
    if (fail) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      data[i] = regs[static_cast<uint8_t>(reg + i)];
    }
    return true;
  }

  bool write(uint8_t reg, const uint8_t *data, size_t length) override {
    if (fail) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      const auto r = static_cast<uint8_t>(reg + i);
      regs[r] = data[i];
      writes.emplace_back(r, data[i]);
    }
    return true;
  }
};

void testAxp2101(void) {
  FakeBus bus;
  AXP2101 pmic(bus, AXP2101::railBit(AXP2101::Rail::DCDC1));

  bus.regs[0x03] = 0x4A;
  check(pmic.probe(), "axp2101 probe accepts 0x4A");
  bus.regs[0x03] = 0x47;
  check(!pmic.probe(), "axp2101 probe rejects another chip");

  // REG 34 bits 7:6 are the debug channel select and must be masked off.
  bus.regs[0x34] = 0xC0 | 0x10;
  bus.regs[0x35] = 0x68;
  uint16_t mv = 0;
  check(pmic.batteryVoltage(mv) && mv == 0x1068, "axp2101 vbat is 14-bit mV");

  // The percentage is only valid with a battery and the gauge on.
  uint8_t pct = 0;
  bus.regs[0xA4] = 77;
  bus.regs[0x00] = 0x00;
  bus.regs[0x18] = 0x0A;
  check(!pmic.batteryPercent(pct), "axp2101 percent refused without battery");
  bus.regs[0x00] = 0x08;
  check(pmic.batteryPercent(pct) && pct == 77, "axp2101 percent with battery and gauge");
  bus.regs[0x18] = 0x02;
  check(!pmic.batteryPercent(pct), "axp2101 percent refused with gauge off");
  bus.regs[0x18] = 0x0A;
  bus.regs[0xA4] = 0xFF;
  check(pmic.batteryPercent(pct) && pct == 100, "axp2101 percent clamps to 100");

  bool good = false;
  bus.regs[0x00] = 0x20;
  check(pmic.vbusGood(good) && good, "axp2101 vbus good bit 5");

  AXP2101::Charge state = AXP2101::Charge::STANDBY;
  bus.regs[0x01] = 0x20;
  check(pmic.chargeState(state) && state == AXP2101::Charge::CHARGING, "axp2101 01b charging");
  bus.regs[0x01] = 0x40;
  check(pmic.chargeState(state) && state == AXP2101::Charge::DISCHARGING,
        "axp2101 10b discharging");
  bus.regs[0x01] = 0x00;
  check(pmic.chargeState(state) && state == AXP2101::Charge::STANDBY, "axp2101 00b standby");
  bus.regs[0x01] = 0x60;
  check(!pmic.chargeState(state), "axp2101 11b reserved is refused");

  // Rail enable bits: REG 80 bits 4:0 DCDC1-5, REG 90 ALDO1-4, BLDO1-2,
  // CPUSLDO, DLDO1, REG 91 bit 0 DLDO2.
  struct RailCase {
    AXP2101::Rail rail;
    uint8_t reg;
    uint8_t bit;
  };
  const RailCase rails[] = {
      {AXP2101::Rail::DCDC3,   0x80, 0x04},
      {AXP2101::Rail::DCDC5,   0x80, 0x10},
      {AXP2101::Rail::ALDO1,   0x90, 0x01},
      {AXP2101::Rail::ALDO4,   0x90, 0x08},
      {AXP2101::Rail::BLDO1,   0x90, 0x10},
      {AXP2101::Rail::BLDO2,   0x90, 0x20},
      {AXP2101::Rail::CPUSLDO, 0x90, 0x40},
      {AXP2101::Rail::DLDO1,   0x90, 0x80},
      {AXP2101::Rail::DLDO2,   0x91, 0x01},
  };
  for (const auto &c : rails) {
    bus.regs[c.reg] = 0x00;
    bool on = false;
    check(pmic.setRail(c.rail, true) && bus.regs[c.reg] == c.bit && pmic.railEnabled(c.rail, on)
              && on,
          "axp2101 rail on reg " + std::to_string(c.reg) + " bit " + std::to_string(c.bit));
    bus.regs[c.reg] = 0xFF;
    check(pmic.setRail(c.rail, false) && bus.regs[c.reg] == static_cast<uint8_t>(~c.bit),
          "axp2101 rail off keeps neighbours " + std::to_string(c.bit));
  }

  // A protected rail is never switched off, and nothing is written.
  bus.regs[0x80] = 0x01;
  bus.writes.clear();
  check(!pmic.setRail(AXP2101::Rail::DCDC1, false) && bus.writes.empty() && bus.regs[0x80] == 0x01,
        "axp2101 protected rail stays on");
  check(pmic.setRail(AXP2101::Rail::DCDC1, true), "axp2101 protected rail may be enabled");

  // LDO voltage: 500 mV + 100 mV per code, bits 4:0, ALDO1 at 0x92 .. BLDO2 at 0x97.
  bus.regs[0x95] = 0xE0;
  check(pmic.setLdoVoltage(AXP2101::Rail::ALDO4, 3300) && bus.regs[0x95] == (0xE0 | 28),
        "axp2101 aldo4 3.3 V is code 28 in REG 95");
  check(pmic.setLdoVoltage(AXP2101::Rail::BLDO2, 3500) && bus.regs[0x97] == 30,
        "axp2101 bldo2 3.5 V is code 30 in REG 97");
  check(!pmic.setLdoVoltage(AXP2101::Rail::ALDO1, 3600), "axp2101 ldo above 3.5 V refused");
  check(!pmic.setLdoVoltage(AXP2101::Rail::DCDC1, 3300), "axp2101 dcdc voltage not handled");
  check(!pmic.setLdoVoltage(AXP2101::Rail::DLDO1, 3300), "axp2101 dldo voltage not handled");

  // Backup cell: REG 18 bit 2 and REG 6A bits 2:0 (2.6 V + 100 mV per code).
  bus.regs[0x18] = 0x0A;
  bus.regs[0x6A] = 0x03;
  check(pmic.setBackupCharge(true, 3300) && bus.regs[0x6A] == 0x07 && bus.regs[0x18] == 0x0E,
        "axp2101 backup charge 3.3 V enabled");
  check(pmic.setBackupCharge(false) && bus.regs[0x18] == 0x0A, "axp2101 backup charge off");
  check(!pmic.setBackupCharge(true, 3400), "axp2101 backup above 3.3 V refused");

  // Power key: REG 41 bits 3:2 enable, REG 49 bits 3:0 status, write 1 to clear.
  bus.regs[0x41] = 0xF0;
  check(pmic.enablePowerKeyIrq(true) && bus.regs[0x41] == 0xFC, "axp2101 pek irq enable");
  bus.regs[0x49] = 0x80 | 0x08 | 0x02;
  bus.writes.clear();
  AXP2101::PowerKey key = {};
  check(pmic.readPowerKey(key) && key.shortPress && !key.longPress && key.falling && !key.rising,
        "axp2101 pek decode");
  check(bus.writes.size() == 1 && bus.writes[0].first == 0x49 && bus.writes[0].second == 0x0A,
        "axp2101 pek clears only key bits");
  bus.regs[0x49] = 0x80;
  bus.writes.clear();
  check(pmic.readPowerKey(key) && bus.writes.empty(), "axp2101 pek idle writes nothing");

  bus.regs[0x10] = 0x30;
  check(pmic.powerOff() && bus.regs[0x10] == 0x31, "axp2101 soft power off REG 10 bit 0");

  bus.fail = true;
  check(!pmic.batteryVoltage(mv) && !pmic.setRail(AXP2101::Rail::ALDO2, true),
        "axp2101 bus error is reported");
}

void testBq27220(void) {
  FakeBus bus;
  BQ27220 gauge(bus);
  // Words are little endian at the command code pair.
  bus.regs[0x08] = 0x68;  // 4200 mV
  bus.regs[0x09] = 0x10;
  bus.regs[0x0A] = 0x09;  // BATTPRES | DSG
  bus.regs[0x0B] = 0x00;
  bus.regs[0x0C] = 0x38;  // -200 mA
  bus.regs[0x0D] = 0xFF;
  bus.regs[0x16] = 0x2C;  // 300 min
  bus.regs[0x17] = 0x01;
  bus.regs[0x2C] = 64;
  bus.regs[0x2D] = 0;

  BQ27220::Reading r = {};
  check(gauge.read(r), "bq27220 read");
  check(r.voltageMv == 4200, "bq27220 voltage 0x08");
  check(r.currentMa == -200, "bq27220 current 0x0C signed");
  check(r.percent == 64, "bq27220 soc 0x2C");
  check(r.timeToEmptyMin == 300, "bq27220 tte 0x16");
  check(r.present && r.discharging, "bq27220 status BATTPRES and DSG");
  check(bus.writes.empty(), "bq27220 never writes the gauge");

  bus.regs[0x0A] = 0x00;
  bus.regs[0x16] = 0xFF;
  bus.regs[0x17] = 0xFF;
  check(gauge.read(r) && !r.present && !r.discharging && r.timeToEmptyMin == BQ27220::NO_ESTIMATE,
        "bq27220 no battery, no estimate");

  bus.fail = true;
  check(!gauge.read(r), "bq27220 bus error is reported");
}

void testXl9555(void) {
  FakeBus bus;
  XL9555 io(bus);
  // Power-on defaults: output 0xFF, config 0xFF (Table 6).
  bus.regs[0x02] = 0xFF;
  bus.regs[0x03] = 0xFF;
  bus.regs[0x06] = 0xFF;
  bus.regs[0x07] = 0xFF;

  check(io.setOutput(9, false), "xl9555 set output");
  check(bus.writes.size() == 2 && bus.writes[0] == std::make_pair<uint8_t, uint8_t>(0x03, 0xFD)
            && bus.writes[1] == std::make_pair<uint8_t, uint8_t>(0x07, 0xFD),
        "xl9555 writes the level before the direction, port 1");
  check(io.write(9, true) && bus.regs[0x03] == 0xFF, "xl9555 write high");
  check(io.setInput(9) && bus.regs[0x07] == 0xFF, "xl9555 back to input");

  bus.regs[0x00] = 0x10;
  bool level = false;
  check(io.read(4, level) && level, "xl9555 read port 0 pin 4");
  check(io.read(5, level) && !level, "xl9555 read port 0 pin 5");
  check(!io.read(16, level) && !io.setOutput(16, true) && !io.setInput(16),
        "xl9555 pin 16 refused");
}

class FakeRadio final: public SX1262::Port {
 public:
  std::vector<std::string> events;
  uint32_t busyPolls = 3;  // polls before BUSY drops after reset
  bool asleep = false;
  bool honourSleep = true;
  bool spiOk = true;
  uint32_t delayed = 0;

  void setReset(bool asserted) override {
    events.push_back(asserted ? "reset-low" : "reset-high");
    asleep = false;
  }
  bool busy(void) override {
    if (asleep) {
      return true;
    }
    if (busyPolls > 0) {
      busyPolls--;
      return true;
    }
    return false;
  }
  bool transfer(const uint8_t *tx, size_t length) override {
    std::string s = "spi";
    for (size_t i = 0; i < length; i++) {
      s += " " + std::to_string(tx[i]);
    }
    events.push_back(s);
    if (spiOk && honourSleep && length == 2 && tx[0] == 0x84) {
      asleep = true;
    }
    return spiOk;
  }
  void delayUs(uint32_t us) override {
    delayed += us;
    if (events.empty() || events.back().rfind("delay", 0) != 0) {
      events.push_back("delay");
    }
  }
};

void testSx1262(void) {
  FakeRadio radio;
  check(SX1262::sleep(radio), "sx1262 sleep succeeds");
  check(radio.events.size() >= 4 && radio.events[0] == "reset-low" && radio.events[1] == "delay"
            && radio.events[2] == "reset-high",
        "sx1262 reset pulse first");
  // SetSleep is opcode 0x84 with sleepConfig 0x00 (cold start, RTC off).
  int spi = 0;
  for (const auto &e : radio.events) {
    if (e.rfind("spi", 0) == 0) {
      spi++;
      check(e == "spi 132 0", "sx1262 SetSleep bytes are 0x84 0x00");
    }
  }
  check(spi == 1, "sx1262 exactly one SPI command");
  check(radio.delayed >= 100 + 500, "sx1262 reset pulse and 500 us settle observed");

  FakeRadio stuck;
  stuck.busyPolls = 1000000;
  check(!SX1262::sleep(stuck), "sx1262 BUSY stuck high fails");
  for (const auto &e : stuck.events) {
    check(e.rfind("spi", 0) != 0, "sx1262 no command while BUSY is high");
  }
  check(stuck.delayed <= SX1262::RESET_PULSE_US + SX1262::READY_BUDGET_US,
        "sx1262 ready wait is bounded");

  FakeRadio ignored;
  ignored.honourSleep = false;
  check(!SX1262::sleep(ignored), "sx1262 BUSY low after SetSleep fails");

  FakeRadio broken;
  broken.spiOk = false;
  check(!SX1262::sleep(broken), "sx1262 SPI failure fails");
}

}  // namespace

int main() {
  testAxp2101();
  testBq27220();
  testXl9555();
  testSx1262();
  if (g_Failures == 0) {
    std::cout << "hal-drivers: PASS\n";
    return 0;
  }
  return 1;
}
