// Pins every Board::Profile row to the values the scattered M5.getBoard()
// switches produced before plan 322 replaced them. The expected values are
// copied from the removed switches at port-2 head c4367912, not from
// src/FurbleBoard.cpp:
//
//   Platform::initBattery()        battery caps and capacity
//   Platform::getInstance()        PMIC click streak, Plus2 GPIO4 hold
//   motionWakeGpio(), arm/disarm   GPIO35 direct, GPIO13 through the M5PM1
//   Platform::canTimedWake()       M5PM1 timer, Plus2 RTC alarm
//   knownBatteryBackedRtc()        TimeKeeper RTC backing
//   pinForBoard()                  IR pin
//   isCoreBoard()                  SD slot
//   boardName()                    boot screen name
//   Feedback::initialize()         sound, light, vibration, LED pin
//   UI::UI(), addPowerMenu(), processAutoOff(), processLowBattery()
//                                  brightness floor, modem sleep, power off

#include <cstring>
#include <iostream>
#include <string>

#include "FurbleBoard.h"

using Furble::Board::MotionWake;
using Furble::Board::Profile;
using Furble::Board::TimedWake;

namespace {

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
  bool modemSleep;
  int minBrightness;
  bool sound, light, vibration;
  int ledPin;
};

// clang-format off
const Expected EXPECTED[] = {
  {m5::board_t::board_M5StickC,      "StickC",   true,  true,  true,  true,  95,  true,  9,
   MotionWake::DIRECT,    35, TimedWake::NONE,       -1, true,  false, true,  false, 48,
   false, true,  false, 10},
  {m5::board_t::board_M5StickCPlus,  "StickC+",  true,  true,  true,  true,  120, true,  9,
   MotionWake::DIRECT,    35, TimedWake::NONE,       -1, true,  false, true,  false, 48,
   true,  true,  false, 10},
  {m5::board_t::board_M5StickCPlus2, "StickC+2", true,  true,  false, false, 200, false, 19,
   MotionWake::NONE,      -1, TimedWake::RTC_ALARM,  4,  false, false, true,  false, 32,
   true,  true,  false, 19},
  {m5::board_t::board_M5StickS3,     "StickS3",  true,  true,  false, true,  250, false, 46,
   MotionWake::PMIC_GPIO, 13, TimedWake::PMIC_TIMER, -1, false, false, true,  true,  32,
   true,  false, false, -1},
  {m5::board_t::board_M5Stack,       "Core",     true,  false, false, false, 0,   false, -1,
   MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  false, false, 32,
   true,  false, false, -1},
  {m5::board_t::board_M5StackCore2,  "Core2",    true,  true,  true,  true,  390, true,  -1,
   MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  true,  false, 32,
   true,  false, true,  -1},
  {m5::board_t::board_M5Tough,       "Tough",    true,  true,  true,  true,  390, false, -1,
   MotionWake::NONE,      -1, TimedWake::NONE,       -1, true,  false, true,  false, 32,
   false, false, false, -1},
  // Every other board, the Waveshare ESP32-S3-ETH and the CoreS3 included.
  {m5::board_t::board_unknown,       "M5",       false, false, false, false, 0,   false, -1,
   MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  false, 32,
   false, false, false, -1},
  {m5::board_t::board_M5StackCoreS3, "M5",       false, false, false, false, 0,   false, -1,
   MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  false, 32,
   false, false, false, -1},
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
    check(p.modemSleep == e.modemSleep, n + " modem sleep");
    check(p.minBrightness == e.minBrightness, n + " brightness floor");
    check(p.minBrightness % 16 == 0, n + " brightness floor is a slider step multiple");
    check(p.feedbackSound == e.sound && p.feedbackLight == e.light
              && p.feedbackVibration == e.vibration && p.ledPin == e.ledPin,
          n + " feedback");
  }

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
