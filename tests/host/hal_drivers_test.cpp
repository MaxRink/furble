// Register-level coverage for the pure chip drivers in lib/hal.
//
// Every expected register, bit and command byte below is taken from the
// datasheet cited in the driver header, not from the driver, so a typo in a
// driver constant fails here:
//
//   AXP2101  X-Powers AXP2101 datasheet revision 1.0, sections 6.12.1 and
//            6.13.2
//   BQ27220  TI SLUUBD4A, revised November 2022, sections 2.1 to 2.21, 5.3 and
//            Tables 2-1 and 2-6
//   XL9555   XINLUDA XL9535/XL9555 datasheet Rev 2.3, section 9.2
//   SX1262   Semtech DS.SX1261-2.W.APP Rev 2.2, sections 8.1, 9.3, 13.1.1 and
//            Table 8-2

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <utility>
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
int g_Checks = 0;

void check(bool condition, const std::string &message) {
  g_Checks++;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

/**
 * How a part maps the bytes of one transfer onto its register file.
 *
 * The fake honours each part's own addressing rule instead of walking up the
 * register file, because a linear walk answers bytes the silicon cannot:
 *
 * LINEAR     the next register holds the next byte. The AXP2101 reads REG 34
 *            and 35 as one 16-bit value (section 6.13.2.30).
 * WORD       one standard command is a 16-bit word whose code is even, and the
 *            command pointer stays inside that word (BQ27220 section 2.1,
 *            Table 2-1).
 * PORT_PAIR  the address pointer walks a 16-bit port pair and wraps back
 *            inside it (XL9555 section 9.2.1, Table 6), so 0x03 then 0x02, not
 *            0x03 then 0x04.
 */
enum class AddressMode {
  LINEAR,
  WORD,
  PORT_PAIR,
};

/** Two hex digits, so an event names a register the way the test reads it. */
std::string hex(uint8_t value) {
  constexpr char DIGITS[] = "0123456789ABCDEF";
  std::string out;
  out.push_back(DIGITS[value >> 4]);
  out.push_back(DIGITS[value & 0x0FU]);
  return out;
}

/**
 * Register file with one interleaved event log.
 *
 * `events` records every transfer and every pacing wait in the order the driver
 * issued them, as FakeRadio does: "read 08 2", "delay 66", "read 0A 2". A case
 * can then assert where a wait sits, not only that one happened. Separate read
 * and delay logs cannot: a driver that spends all four gaps before the first
 * packet, or drops the gap before the last, fills them the same way as a
 * correct one.
 *
 * `writes` stays as a total for the cases that only ask whether anything was
 * written. `misuses` collects every transfer the part's addressing rule cannot
 * serve, such as an odd BQ27220 command code or a read that would have to leave
 * a 16-bit pair; a passing case asserts it stays empty.
 */
class FakeBus final: public RegisterBus {
 public:
  explicit FakeBus(AddressMode mode = AddressMode::LINEAR) : m_Mode(mode) {}

  std::map<uint8_t, uint8_t> regs;
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  std::vector<std::string> events;
  std::vector<std::string> misuses;
  bool fail = false;

  bool read(uint8_t reg, uint8_t *data, size_t length) override {
    if (fail) {
      return false;
    }
    note(reg, length);
    events.push_back("read " + hex(reg) + ' ' + std::to_string(length));
    for (size_t i = 0; i < length; i++) {
      data[i] = regs[address(reg, i)];
    }
    return true;
  }

  bool write(uint8_t reg, const uint8_t *data, size_t length) override {
    if (fail) {
      return false;
    }
    note(reg, length);
    events.push_back("write " + hex(reg) + ' ' + std::to_string(length));
    for (size_t i = 0; i < length; i++) {
      const uint8_t r = address(reg, i);
      regs[r] = data[i];
      writes.emplace_back(r, data[i]);
    }
    return true;
  }

  void delayUs(uint32_t us) override { events.push_back("delay " + std::to_string(us)); }

 private:
  /** Register that holds byte `i` of a transfer started at `reg`. */
  uint8_t address(uint8_t reg, size_t i) const {
    switch (m_Mode) {
      case AddressMode::WORD:
        return static_cast<uint8_t>((reg & 0xFEU) + (i % 2));
      case AddressMode::PORT_PAIR:
        return static_cast<uint8_t>((i % 2) == 0 ? reg : (reg ^ 0x01U));
      case AddressMode::LINEAR:
      default:
        return static_cast<uint8_t>(reg + i);
    }
  }

  void note(uint8_t reg, size_t length) {
    if (m_Mode == AddressMode::WORD) {
      if ((reg & 0x01U) != 0) {
        misuses.push_back("odd standard command code " + std::to_string(reg));
      }
      if (length > 2) {
        misuses.push_back("standard command read wider than its word");
      }
    } else if ((m_Mode == AddressMode::PORT_PAIR) && (length > 2)) {
      misuses.push_back("XL9555 transfer wider than one port pair");
    }
  }

  const AddressMode m_Mode;
};

/** The log with each delay's length dropped, so only the placement shows. */
std::vector<std::string> eventKinds(const std::vector<std::string> &events) {
  std::vector<std::string> kinds;
  for (const auto &event : events) {
    kinds.push_back(event.rfind("delay ", 0) == 0 ? "delay" : event);
  }
  return kinds;
}

/** The read events of a log, in the order the driver issued them. */
std::vector<std::string> readEvents(const std::vector<std::string> &events) {
  std::vector<std::string> reads;
  for (const auto &event : events) {
    if (event.rfind("read ", 0) == 0) {
      reads.push_back(event);
    }
  }
  return reads;
}

/**
 * Protected mask for DCDC1. Rail order puts DCDC1 first and its enable is REG
 * 80 bit 0 (section 6.13.2.68), so the mask names it with bit 0 of the
 * uint16_t rather than with a register bit.
 */
constexpr uint16_t PROTECTED_DCDC1 = 0x0001;

bool sameKey(const AXP2101::PowerKey &a, const AXP2101::PowerKey &b) {
  return (a.shortPress == b.shortPress) && (a.longPress == b.longPress) && (a.falling == b.falling)
         && (a.rising == b.rising);
}

/** Datasheet register pairs map onto the fake's own addressing rule. */
void testBusModel(void) {
  // BQ27220 section 2.1: a standard command is a two-byte word, least
  // significant byte first, and Table 2-1 lists it at an even command code. A
  // third byte re-reads the word instead of moving to the next command.
  FakeBus word(AddressMode::WORD);
  word.regs[0x08] = 0x68;
  word.regs[0x09] = 0x10;
  word.regs[0x0A] = 0x09;
  word.regs[0x0B] = 0x00;
  uint8_t data[4] = {};
  check(word.read(0x08, data, 3) && data[0] == 0x68 && data[1] == 0x10 && data[2] == 0x68,
        "fake bus wraps inside a BQ27220 command word");
  check(word.misuses.size() == 1, "fake bus flags a read wider than one command word");
  word.regs[0x16] = 0x16;
  word.regs[0x17] = 0x17;
  word.regs[0x18] = 0xAA;
  check(word.read(0x17, data, 2) && data[0] == 0x16 && data[1] == 0x17,
        "fake bus does not walk an odd command code into the next command");
  check(word.misuses.size() == 2, "fake bus flags an odd BQ27220 command code");

  // XL9555 section 9.2.1: the pointer wraps inside the register pair, so a
  // two-byte read from 0x03 answers both output ports.
  FakeBus port(AddressMode::PORT_PAIR);
  port.regs[0x02] = 0xA5;
  port.regs[0x03] = 0x5A;
  port.regs[0x04] = 0xEE;  // polarity port 0, never part of the output pair
  check(port.read(0x03, data, 2) && data[0] == 0x5A && data[1] == 0xA5,
        "fake bus wraps the XL9555 output pair");
  check(port.misuses.empty(), "fake bus serves the XL9555 pair without a misuse");

  // AXP2101 section 6.13.2.30: REG 34 and 35 are one 16-bit value.
  FakeBus linear;
  linear.regs[0x34] = 0x10;
  linear.regs[0x35] = 0x68;
  check(linear.read(0x34, data, 2) && data[0] == 0x10 && data[1] == 0x68,
        "fake bus walks the AXP2101 VBAT word linearly");
}

void testAxp2101(void) {
  FakeBus bus;
  AXP2101 pmic(bus, PROTECTED_DCDC1);

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
      {AXP2101::Rail::DCDC2,   0x80, 0x02},
      {AXP2101::Rail::DCDC3,   0x80, 0x04},
      {AXP2101::Rail::DCDC4,   0x80, 0x08},
      {AXP2101::Rail::DCDC5,   0x80, 0x10},
      {AXP2101::Rail::ALDO1,   0x90, 0x01},
      {AXP2101::Rail::ALDO2,   0x90, 0x02},
      {AXP2101::Rail::ALDO3,   0x90, 0x04},
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

  // A protected rail is never switched off, and nothing is written. DCDC1 is
  // the first rail the mask can name and its enable is REG 80 bit 0
  // (section 6.13.2.68).
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

  // Power key: REG 41 bits 3:0 enable the four events REG 49 bits 3:0 latch
  // (sections 6.13.2.41 and 6.13.2.44). Section 6.13.2.41 leaves bits 1:0 at
  // 0 and section 6.12.1 sets a status bit only when its enable bit is set, so
  // an enable of bits 3:2 alone would leave the two edge events unreachable.
  constexpr uint8_t KEY_EVENTS = 0x0F;
  bus.regs[0x41] = 0xF0;
  check(pmic.enablePowerKeyIrq(true) && bus.regs[0x41] == 0xFF,
        "axp2101 pek irq enables all four key bits");
  check(pmic.enablePowerKeyIrq(false) && bus.regs[0x41] == 0xF0,
        "axp2101 pek irq off clears only the key bits");

  struct KeyBit {
    uint8_t bit;
    bool AXP2101::PowerKey::*field;
    const char *name;
  };
  const KeyBit keyBits[] = {
      {0x01, &AXP2101::PowerKey::rising,     "rising edge" },
      {0x02, &AXP2101::PowerKey::falling,    "falling edge"},
      {0x04, &AXP2101::PowerKey::longPress,  "long press"  },
      {0x08, &AXP2101::PowerKey::shortPress, "short press" },
  };

  // The enable mask and the decoded bits must agree, or the API offers an
  // event the part never latches.
  check(pmic.enablePowerKeyIrq(true), "axp2101 pek irq enabled for the decode cases");
  uint8_t enabledMask = 0;
  for (const auto &c : keyBits) {
    enabledMask = static_cast<uint8_t>(enabledMask | c.bit);
  }
  check((bus.regs[0x41] & KEY_EVENTS) == enabledMask, "axp2101 enables every bit it decodes");
  check((bus.regs[0x41] & 0xF0) == 0xF0, "axp2101 leaves the other REG 41 bits alone");

  for (const auto &c : keyBits) {
    // Bit 7 of REG 49 is another event, not a power key one.
    bus.regs[0x49] = static_cast<uint8_t>(0x80 | c.bit);
    bus.writes.clear();
    AXP2101::PowerKey key = {};
    AXP2101::PowerKey expected = {};
    expected.*c.field = true;
    check(pmic.readPowerKey(key) && sameKey(key, expected),
          std::string("axp2101 decodes ") + c.name);
    check(bus.writes.size() == 1 && bus.writes[0] == std::pair<uint8_t, uint8_t>(0x49, c.bit),
          std::string("axp2101 clears only the ") + c.name + " bit");
  }

  bus.regs[0x49] = 0x80 | 0x08 | 0x02;
  bus.writes.clear();
  AXP2101::PowerKey key = {};
  check(pmic.readPowerKey(key) && key.shortPress && !key.longPress && key.falling && !key.rising,
        "axp2101 pek decode");
  check(bus.writes.size() == 1 && bus.writes[0] == std::pair<uint8_t, uint8_t>(0x49, 0x0A),
        "axp2101 pek clears only key bits");
  bus.regs[0x49] = 0x80;
  bus.writes.clear();
  check(pmic.readPowerKey(key) && bus.writes.empty(), "axp2101 pek idle writes nothing");

  // Section 6.13.2.7: soft power off is REG 10 bit 0 and nothing else. The key
  // latches are not drained here (see powerOff() in the header), so the press
  // that powers the device off stays in REG 49 for the next boot to read.
  bus.regs[0x10] = 0x30;
  bus.regs[0x49] = 0x0B;
  bus.writes.clear();
  check(pmic.powerOff() && bus.regs[0x10] == 0x31, "axp2101 soft power off REG 10 bit 0");
  check(bus.regs[0x49] == 0x0B && bus.writes.size() == 1
            && bus.writes[0] == std::pair<uint8_t, uint8_t>(0x10, 0x31),
        "axp2101 power off does not drain the key latches");

  bus.fail = true;
  check(!pmic.batteryVoltage(mv) && !pmic.setRail(AXP2101::Rail::ALDO2, true),
        "axp2101 bus error is reported");
}

void testBq27220(void) {
  FakeBus bus(AddressMode::WORD);
  BQ27220 gauge(bus);
  // Words are little endian at the command code pair, Table 2-1.
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
  check(bus.misuses.empty(), "bq27220 stays inside one command word per packet");

  // The five standard commands of Table 2-1, each a two-byte word.
  const std::vector<std::string> packets = {"read 08 2", "read 0A 2", "read 0C 2", "read 16 2",
                                            "read 2C 2"};
  check(readEvents(bus.events) == packets, "bq27220 reads the five standard commands once each");

  // Section 5.3: 66 us of bus-free time between packets, so the log has to
  // alternate packet and gap, read then delay then read. One interleaved log is
  // what separates "before each packet" from "somewhere in the read": four gaps
  // ahead of the first packet, and a missing gap before the last, both fill
  // separate read and delay logs exactly as a correct driver does.
  const std::vector<std::string> paced = {"read 08 2", "delay",     "read 0A 2",
                                          "delay",     "read 0C 2", "delay",
                                          "read 16 2", "delay",     "read 2C 2"};
  check(eventKinds(bus.events) == paced,
        "bq27220 leaves a gap before every packet after the first");

  // Four waits for five packets, each at least the document's 66 us.
  size_t gaps = 0;
  for (const auto &event : bus.events) {
    if (event.rfind("delay ", 0) == 0) {
      gaps++;
      check(std::stoul(event.substr(6)) >= 66, "bq27220 leaves the bus free at least 66 us");
    }
  }
  check(gaps == 4, "bq27220 waits between packets only");
  // Section 5.3 also caps the poll rate at twice a second, which the driver
  // states rather than enforces.
  check(BQ27220::MIN_POLL_INTERVAL_MS >= 500, "bq27220 poll interval honours the 2 Hz cap");

  bus.regs[0x0A] = 0x00;
  bus.regs[0x16] = 0xFF;
  bus.regs[0x17] = 0xFF;
  // Section 2.11: 0xFFFF means the gauge has no time-to-empty estimate.
  check(gauge.read(r) && !r.present && !r.discharging && r.timeToEmptyMin == 0xFFFF,
        "bq27220 no battery, no estimate");

  BQ27220::Reading kept = {4200, -200, 64, 300, true, true};
  bus.fail = true;
  check(!gauge.read(kept) && kept.voltageMv == 4200 && kept.percent == 64,
        "bq27220 leaves the reading untouched on a bus error");
}

void testXl9555(void) {
  FakeBus bus(AddressMode::PORT_PAIR);
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
  check(bus.misuses.empty(), "xl9555 accesses stay inside one register pair");
}

class FakeRadio final: public SX1262::Port {
 public:
  std::vector<std::string> events;
  uint32_t busyPolls = 3;          // polls with BUSY high before it drops after reset
  uint32_t sleepConfirmPolls = 0;  // polls with BUSY still low after SetSleep
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
      if (sleepConfirmPolls > 0) {
        sleepConfirmPolls--;
        return false;
      }
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
    events.push_back("delay " + std::to_string(us));
  }
};

uint32_t delayAfter(const std::vector<std::string> &events, const std::string &marker) {
  for (size_t i = 0; i + 1 < events.size(); i++) {
    if ((events[i] == marker) && (events[i + 1].rfind("delay ", 0) == 0)) {
      return static_cast<uint32_t>(std::stoul(events[i + 1].substr(6)));
    }
  }
  return 0;
}

/**
 * The board's power-off path allows 20 ms between rail decisions, so the whole
 * sleep sequence must end inside that. Neither Table 8-2 nor section 13.1.1
 * bounds the standby-to-sleep time, so this is the only stated limit.
 */
constexpr uint32_t SLEEP_SEQUENCE_BUDGET_US = 20'000;

/**
 * The bound SX1262.h states for the whole sequence: 200 us of reset, 10,000 us
 * waiting for the post-reset calibration, 500 us of no-SPI settle and 5,000 us
 * confirming sleep, so 15,700 us at worst.
 *
 * Only the 500 us comes from the datasheet (section 13.1.1). The two budgets
 * are driver choices for transitions no cited table times, so the header's
 * total is the contract a board budgets its power-off path against. Summing the
 * header's own constants against that total keeps the paragraph honest: with no
 * such check a 9,000 us confirm budget or a 14,000 us ready budget passes every
 * other case here and quietly contradicts the document.
 */
constexpr uint32_t DOCUMENTED_SLEEP_BOUND_US = 15'700;
constexpr uint32_t HEADER_SLEEP_BOUND_US = SX1262::RESET_PULSE_US + SX1262::READY_BUDGET_US
                                           + SX1262::SLEEP_SETTLE_US
                                           + SX1262::SLEEP_CONFIRM_BUDGET_US;

void testSx1262(void) {
  // The four budgets the header's steps name, then their sum against the total
  // it states.
  check(SX1262::RESET_PULSE_US == 200 && SX1262::READY_BUDGET_US == 10'000
            && SX1262::SLEEP_SETTLE_US == 500 && SX1262::SLEEP_CONFIRM_BUDGET_US == 5'000,
        "sx1262 budgets are the numbers the header states");
  check(HEADER_SLEEP_BOUND_US == DOCUMENTED_SLEEP_BOUND_US,
        "sx1262 header total bound is the documented 15,700 us");

  FakeRadio radio;
  check(SX1262::sleep(radio), "sx1262 sleep succeeds");
  check(radio.events.size() >= 4 && radio.events[0] == "reset-low"
            && radio.events[1].rfind("delay ", 0) == 0 && radio.events[2] == "reset-high",
        "sx1262 reset pulse first");
  // Section 8.1 needs typically 100 us of NRESET, so a zero-width pulse would
  // leave the part in whatever state the previous owner left it.
  check(delayAfter(radio.events, "reset-low") >= 100, "sx1262 reset pulse is at least 100 us");
  // SetSleep is opcode 0x84 with sleepConfig 0x00 (cold start, RTC off).
  int spi = 0;
  for (const auto &e : radio.events) {
    if (e.rfind("spi", 0) == 0) {
      spi++;
      check(e == "spi 132 0", "sx1262 SetSleep bytes are 0x84 0x00");
    }
  }
  check(spi == 1, "sx1262 exactly one SPI command");
  // Section 13.1.1: the chip must not see SPI for 500 us after SetSleep.
  check(delayAfter(radio.events, "spi 132 0") >= 500, "sx1262 keeps SPI off for 500 us");
  check(radio.delayed <= SLEEP_SEQUENCE_BUDGET_US, "sx1262 sequence inside the stated bound");

  // Table 8-2 tabulates no standby-to-sleep time, so BUSY may arrive late; a
  // part that asserts it after the settle window is healthy.
  FakeRadio late;
  late.busyPolls = 0;
  late.sleepConfirmPolls = 10;  // BUSY stays low for 1 ms after SetSleep
  check(SX1262::sleep(late), "sx1262 sleep tolerates a late BUSY");
  check(late.delayed <= SLEEP_SEQUENCE_BUDGET_US, "sx1262 late BUSY stays inside the bound");

  FakeRadio stuck;
  stuck.busyPolls = 1000000;
  check(!SX1262::sleep(stuck), "sx1262 BUSY stuck high fails");
  for (const auto &e : stuck.events) {
    check(e.rfind("spi", 0) != 0, "sx1262 no command while BUSY is high");
  }
  // A stuck BUSY spends the reset pulse plus the whole ready budget and returns
  // before SetSleep, so 10,200 us is its real cost. The bound here is the
  // documented total on purpose: the first version restated the driver's own
  // constants, and the four cold starts plus reset bound that replaced them
  // (14,200 us from Table 8-2) was looser than the path it guarded, so a
  // 14,000 us ready budget fitted it. The two checks above are what hold the
  // header at 15,700 us; this case proves the wait stops inside it.
  check(stuck.delayed <= DOCUMENTED_SLEEP_BOUND_US,
        "sx1262 stuck BUSY stays inside the documented total");

  FakeRadio ignored;
  ignored.honourSleep = false;
  check(!SX1262::sleep(ignored), "sx1262 BUSY low after SetSleep fails");
  check(ignored.delayed <= SLEEP_SEQUENCE_BUDGET_US, "sx1262 confirms BUSY high inside the bound");

  FakeRadio broken;
  broken.spiOk = false;
  check(!SX1262::sleep(broken), "sx1262 SPI failure fails");
}

}  // namespace

int main() {
  testBusModel();
  testAxp2101();
  testBq27220();
  testXl9555();
  testSx1262();
  if (g_Failures == 0) {
    std::cout << "hal-drivers: PASS (" << g_Checks << " checks)\n";
    return 0;
  }
  return 1;
}
