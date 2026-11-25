// Register policy for the IP5306 boost converter on the Core.
//
// Every bit asserted here comes from the Injoinic IP5306 register document
// V1.21 (revision V1.4), hosted by M5Stack, not from the header itself:
//
//   SYS_CTL0 (0x00) bit 1  "BOOST output normally open", 0 disable / 1 enable
//   SYS_CTL2 (0x02) 3:2    light-load shutdown time, 00 8 s, 01 32 s,
//                          10 16 s, 11 64 s
//
// M5Unified 0.2.20 writes SYS_CTL0 = 0x31 and SYS_CTL2 = 0x6C at
// M5.begin() for board_M5Stack. The first clears the keep-on bit, so without
// this policy a panel-off Core falls below 45 mA and the PMIC removes the 5 V
// boost. There is no PMIC shutdown command to clear the bit in later, so it
// must stay set for the whole session.

#include <cstdint>
#include <iostream>
#include <string>

#include "FurbleIP5306.h"

namespace {

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

// The m5::IP5306_Class surface holdBoostOn() uses.
class FakePmic {
 public:
  bool setPowerBoostKeepOn(bool enable) {
    writes++;
    if (writeFails) {
      return false;
    }
    if (enable) {
      regs[Furble::IP5306::REG_SYS_CTL0] |= Furble::IP5306::SYS_CTL0_BOOST_KEEP_ON;
    } else {
      regs[Furble::IP5306::REG_SYS_CTL0] &= ~Furble::IP5306::SYS_CTL0_BOOST_KEEP_ON;
    }
    if (ignoreWrite) {
      regs[Furble::IP5306::REG_SYS_CTL0] = bootValue;
    }
    return true;
  }

  uint8_t readRegister8(uint8_t reg) const {
    return (readFails || (reg >= sizeof(regs))) ? 0 : regs[reg];
  }

  uint8_t regs[2] = {0x31, 0x6C};
  uint8_t bootValue = 0x31;
  bool writeFails = false;
  bool ignoreWrite = false;
  bool readFails = false;
  int writes = 0;
};

}  // namespace

int main() {
  using Furble::IP5306::boostKeptOn;
  using Furble::IP5306::lightLoadSeconds;
  using Furble::IP5306::withBoostKeptOn;
  using Furble::IP5306::withLongestLightLoad;

  // The register values M5Unified 0.2.20 writes on the Core.
  check(!boostKeptOn(0x31), "0x31 leaves the BOOST normally-open bit clear");
  check(withBoostKeptOn(0x31) == 0x33, "keep-on changes only SYS_CTL0 bit 1");
  check(lightLoadSeconds(0x6C) == 64, "M5Unified's 0x6C selects the 64 s window");

  // Every SYS_CTL0 byte: the bit goes on, every other bit is preserved.
  for (int value = 0; value < 256; value++) {
    const auto raw = static_cast<uint8_t>(value);
    const uint8_t held = withBoostKeptOn(raw);
    check(boostKeptOn(held), "keep-on reads back set for every byte");
    check((held & ~0x02) == (raw & ~0x02), "keep-on preserves every other SYS_CTL0 bit");
  }

  // The light-load field decodes per the register document, and the longest
  // window write touches only bits 3:2.
  check(lightLoadSeconds(0x00) == 8, "00 is 8 s");
  check(lightLoadSeconds(0x04) == 32, "01 is 32 s");
  check(lightLoadSeconds(0x08) == 16, "10 is 16 s");
  check(lightLoadSeconds(0x0C) == 64, "11 is 64 s");
  check(withLongestLightLoad(0x60) == 0x6C, "the longest window keeps the other bits");
  check(lightLoadSeconds(withLongestLightLoad(0x60)) == 64, "the longest window reads back 64 s");

  // The session policy: write, then prove the bit survived the write.
  {
    FakePmic pmic;
    check(Furble::IP5306::holdBoostOn(pmic), "a readable keep-on bit is accepted");
    check(pmic.regs[Furble::IP5306::REG_SYS_CTL0] == 0x33, "the policy leaves SYS_CTL0 0x33");
    check(pmic.writes == 1, "the policy writes the keep-on bit once");
  }
  {
    FakePmic pmic;
    pmic.writeFails = true;
    check(!Furble::IP5306::holdBoostOn(pmic), "a failed I2C write is a failure");
  }
  {
    FakePmic pmic;
    pmic.ignoreWrite = true;
    check(!Furble::IP5306::holdBoostOn(pmic), "a write that does not stick is a failure");
  }
  {
    FakePmic pmic;
    pmic.readFails = true;
    check(!Furble::IP5306::holdBoostOn(pmic), "an unreadable register is a failure");
  }

  if (g_Failures == 0) {
    std::cout << "ip5306-policy: PASS\n";
    return 0;
  }
  return 1;
}
