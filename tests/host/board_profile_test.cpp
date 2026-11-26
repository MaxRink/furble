// Pins every Board::Profile row to the values the scattered M5.getBoard()
// switches produced before plan 322 replaced them, plus the hardware
// corrections in plan 354. The expected values are copied from the removed
// switches at port-2 head c4367912, not from src/FurbleBoard.cpp:
//
//   Platform::initBattery()        battery caps and capacity
//   Platform::getInstance()        PMIC click streak, Plus2 GPIO4 hold
//   motionWakeGpio(), arm/disarm   GPIO35 direct, GPIO13 through the M5PM1
//   Platform::canTimedWake()       M5PM1 timer, Plus2 and Tough RTC alarm
//   knownBatteryBackedRtc()        TimeKeeper RTC backing
//   pinForBoard()                  IR pin
//   isCoreBoard()                  SD slot
//   boardName()                    boot screen name
//   Feedback::initialize()         sound, light, vibration, LED pin
//   UI::UI(), addPowerMenu(), processAutoOff(), processLowBattery()
//                                  brightness floor, modem sleep, power off
//
// Plan 354 corrects three rows against the vendor hardware, not against the old
// switches:
//
//   Core  capacity 110 mAh    M5Stack Basic page, "Battery 110 mAh @ 3.7 V"
//   Core2 capacity 500 mAh    M5Stack Core2 page, changed from 390 mAh in 2023.10
//   Tough sd, sound, RTC      M5Stack Tough K034 features and pinmap: microSD
//                             slot, NS4168 1 W speaker on AXP192 GPIO2, and a
//                             battery-backed BM8563 whose INT drives the AXP192
//                             power key (AXP_PWR on the Tough pinmap)
//
// The GPS rail column is copied from the boards that M5Unified 0.2.20
// Power_Class::setExtOutput() switches; every other board furble supports
// falls through to its empty default. On the Core2 and Tough it switches one
// BUS_5V net shared by Grove Port A and the M-Bus.
//
// Deviations from the port-2 copy (plan 350). The Plus2 RTC backing row is
// true: the board keeps its BM8563 alive across a HOLD power-off so the alarm
// can wake it, so TimeKeeper applies the BM8563 voltage-low flag and the backed
// uncertainty there, where `knownBatteryBackedRtc()` returned false. The
// StickC Plus timed wake row is RTC_ALARM because its BM8563 INT reaches the
// AXP192 power key through R401 and C401. The LED and IR polarity columns are
// new: the StickC and Plus emitter cathodes are on the pin while the Plus2
// drives an NPN base, so both rows differ between those families.

#include <cstring>
#include <iostream>
#include <string>

#include "FurbleBoard.h"
#include "FurblePinShare.h"

using Furble::Board::MotionWake;
using Furble::Board::Profile;
using Furble::Board::TimedWake;

namespace {

// meetsPowerBackend() must accept the documented signatures and reject a
// different signature or a virtual implementation.
struct GoodBackend: Furble::PowerBackend {
  battery_caps_t getBatteryCaps(void);
  battery_t readBattery(void);
  uint16_t getBatteryCapacity(void);
  bool setRail(Rail rail, bool on);
  uint8_t getPWRClickCount(void);
  bool powerOff(void);
  bool canTimedWake(void);
  bool powerOffUntil(uint32_t seconds);
  bool consumeTimedWake(void);
};
// Each negative case differs from GoodBackend in exactly one way.
struct WrongSignature: Furble::PowerBackend {
  battery_caps_t getBatteryCaps(void);
  battery_t readBattery(void);
  uint16_t getBatteryCapacity(void);
  bool setRail(Rail rail, bool on);
  uint8_t getPWRClickCount(void);
  bool powerOff(void);
  bool canTimedWake(void);
  bool powerOffUntil(uint16_t seconds);
  bool consumeTimedWake(void);
};
struct VirtualBackend: Furble::PowerBackend {
  virtual ~VirtualBackend() = default;
  battery_caps_t getBatteryCaps(void);
  battery_t readBattery(void);
  uint16_t getBatteryCapacity(void);
  bool setRail(Rail rail, bool on);
  uint8_t getPWRClickCount(void);
  bool powerOff(void);
  bool canTimedWake(void);
  bool powerOffUntil(uint32_t seconds);
  bool consumeTimedWake(void);
};
static_assert(Furble::meetsPowerBackend<GoodBackend>(), "documented signatures pass");
static_assert(!Furble::meetsPowerBackend<WrongSignature>(), "a changed signature fails");
static_assert(!Furble::meetsPowerBackend<VirtualBackend>(), "virtual dispatch fails");

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

struct Expected {
  m5::board_t board;
  const char *name;
  bool level, voltage, current, charging;
  uint16_t capacity;
  bool rtcBacked;
  int irPin;
  MotionWake motion;
  int motionPin;
  TimedWake timed;
  int holdPin;
  bool clickStreak;
  bool sd;
  bool canPowerOff;
  bool powerOffPath;
  bool modemSleep;
  int minBrightness;
  bool sound, light, vibration;
  int ledPin;
  bool gpsRail;
  bool ledActiveLow;
  bool irMarkLow;
};

// clang-format off
const Expected EXPECTED[] = {
    {m5::board_t::board_M5StickC, "StickC", true, true, true, true, 95, true, 9, MotionWake::DIRECT, 35, TimedWake::NONE, -1, true, false, true, true, false, 48, false, true, false, 10, true, true, true},
    {m5::board_t::board_M5StickCPlus, "StickC+", true, true, true, true, 120, true, 9, MotionWake::DIRECT, 35, TimedWake::RTC_ALARM, -1, true, false, true, true, false, 48, true, true, false, 10, true, true, true},
    {m5::board_t::board_M5StickCPlus2, "StickC+2", true, true, false, false, 200, true, 19, MotionWake::NONE, -1, TimedWake::RTC_ALARM, 4, false, false, true, true, false, 32, true, true, false, 19, false, false, false},
    {m5::board_t::board_M5StickS3, "StickS3", true, true, false, true, 250, false, 46, MotionWake::PMIC_GPIO, 13, TimedWake::PMIC_TIMER, -1, false, false, true, true, true, 32, true, false, false, -1, true, true, false},
    {m5::board_t::board_M5Stack, "Core", true, false, false, false, 110, false, -1, MotionWake::NONE, -1, TimedWake::NONE, -1, false, true, false, true, false, 32, true, false, false, -1, false, true, false},
    {m5::board_t::board_M5StackCore2, "Core2", true, true, true, true, 500, true, -1, MotionWake::NONE, -1, TimedWake::NONE, -1, false, true, true, true, false, 32, true, false, true, -1, true, true, false},
    {m5::board_t::board_M5Tough, "Tough", true, true, true, true, 390, true, -1, MotionWake::NONE, -1, TimedWake::RTC_ALARM, -1, true, true, true, true, false, 32, true, false, false, -1, true, true, false},
  // The Waveshare ESP32-S3-ETH row has no removed switch to copy from. It pins
  // the board facts recorded in plans/349-waveshare-board-row.md: no PMIC, no
  // battery, no RTC part, no IR emitter, no IMU, no speaker, no vibration motor,
  // no switchable Port A rail and no software power-off path. The microSD slot
  // exists but no card select is mapped, so the row reports no SD slot while
  // FURBLE_HAS_SD stays 1 for that image.
  {m5::board_t::board_M5StampS3, "Waveshare", false, false, false, false, 0, false, -1,
   MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, false, false, false, 32,
   false, false, false, -1, false, false, false},
  // Every other board the CoreS3 included.
    {m5::board_t::board_unknown, "M5", false, false, false, false, 0, false, -1, MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, true, true, false, 32, false, false, false, -1, false, true, false},
    {m5::board_t::board_M5StackCoreS3, "M5", false, false, false, false, 0, false, -1, MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, true, true, false, 32, false, false, false, -1, false, true, false},
};
// clang-format on

}  // namespace

int main() {
  for (const auto &e : EXPECTED) {
    const Profile &p = Furble::Board::profileFor(e.board);
    const std::string n = e.name;
    check(std::strcmp(p.name, e.name) == 0, n + " name");
    check(p.battery.level == e.level && p.battery.voltage == e.voltage
              && p.battery.current == e.current && p.battery.charging == e.charging,
          n + " battery caps");
    check(p.batteryCapacityMah == e.capacity, n + " capacity");
    check(p.rtcBatteryBacked == e.rtcBacked, n + " rtc backing");
    check(p.irPin == e.irPin, n + " ir pin");
    check(p.motionWake == e.motion && p.motionWakePin == e.motionPin, n + " motion wake");
    check(p.timedWake == e.timed, n + " timed wake");
    check(p.powerHoldPin == e.holdPin, n + " hold pin");
    check(p.pmicClickStreak == e.clickStreak, n + " click streak");
    check(p.sdSlot == e.sd, n + " sd slot");
    check(p.canPowerOff == e.canPowerOff, n + " power off");
    check(p.powerOffPath == e.powerOffPath, n + " power off path");
    check(p.modemSleep == e.modemSleep, n + " modem sleep");
    check(p.minBrightness == e.minBrightness, n + " brightness floor");
    check(p.minBrightness % 16 == 0, n + " brightness floor is a slider step multiple");
    check(p.feedbackSound == e.sound && p.feedbackLight == e.light
              && p.feedbackVibration == e.vibration && p.ledPin == e.ledPin,
          n + " feedback");
    check(p.gpsRail == e.gpsRail, n + " gps rail");
    check(p.ledActiveLow == e.ledActiveLow, n + " led polarity");
    check(p.irMarkLow == e.irMarkLow, n + " ir mark polarity");
  }

  // Only the Plus2 puts the LED and the IR emitter on one pin, which is what
  // selects the PinShare hand-over; every other supported board keeps them
  // apart or has no emitter (plan 350).
  const Profile &plus2 = Furble::Board::profileFor(m5::board_t::board_M5StickCPlus2);
  check(Furble::PinShare::ledSharesIr(plus2.ledPin, plus2.irPin),
        "the Plus2 LED and IR emitter share G19");
  for (const auto &e : EXPECTED) {
    if (e.board == m5::board_t::board_M5StickCPlus2) {
      continue;
    }
    const Profile &other = Furble::Board::profileFor(e.board);
    check(!Furble::PinShare::ledSharesIr(other.ledPin, other.irPin),
          std::string(e.name) + " does not share the LED and IR pins");
  }

  // Plan 354: these are hardware facts from the vendor documents cited in the
  // header comment, asserted by name so a table edit cannot quietly drop them.
  const Profile &tough = Furble::Board::profileFor(m5::board_t::board_M5Tough);
  check(tough.sdSlot, "Tough keeps the microSD slot");
  check(tough.feedbackSound, "Tough has the NS4168 1 W speaker");
  check(tough.rtcBatteryBacked, "Tough BM8563 is backed by BT1 BATTERY_RTC");
  check(tough.timedWake == TimedWake::RTC_ALARM, "Tough can wake on the BM8563 timer");
  // The two power-off columns answer different questions and must not be
  // collapsed: the Core offers no automatic policy but keeps its IP5306 Off
  // (plans/349, plans/354 review finding 2).
  const Profile &core = Furble::Board::profileFor(m5::board_t::board_M5Stack);
  check(!core.canPowerOff, "the Core row offers no auto-off or low-battery policy");
  check(core.powerOffPath, "the Core row keeps its deferred IP5306 power-off path");

  // profile() is the row for the detected board.
  M5.board = m5::board_t::board_M5StickCPlus2;
  check(&Furble::Board::profile() == &Furble::Board::profileFor(m5::board_t::board_M5StickCPlus2),
        "profile follows the detected board");

  if (g_Failures == 0) {
    std::cout << "board-profile: PASS\n";
    return 0;
  }
  return 1;
}
