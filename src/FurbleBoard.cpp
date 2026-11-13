#include "FurbleBoard.h"

namespace Furble {
namespace Board {

namespace {
// Battery capacities are from the vendor product pages. Battery caps follow
// the PMIC: AXP192 boards report all four, the M5PM1 has no battery current
// register, the Plus2 reads an ADC divider and the Core's IP5306 gives a
// coarse level only.
//
// IR: G9 is only free on the StickC and StickC Plus because they run the flash
// in DIO mode; QIO would claim GPIO 9/10 as SD2/SD3. G19 on the Plus2 is also
// the LED. Only the S3 image runs on a StickS3, and the S3 has GPIO 46.
//
// Motion wake: the StickC family MPU6886 INT is GPIO35, shared with the BM8563
// on SYS_INT. The StickS3 BMI270 INT1 goes to M5PM1 GPIO4, and the PMIC drives
// PYG1_IRQ into GPIO13.

// Columns: name, battery {level, voltage, current, charging}, capacity mAh,
// RTC backed, IR pin, motion wake, motion pin, timed wake, hold pin, PMIC
// click streak, SD slot, can power off, modem sleep, brightness floor,
// feedback sound, light, vibration, LED pin.
//
// clang-format off
constexpr Profile STICKC       = {"StickC",   {true,  true,  true,  true},  95,  true,  9,
    MotionWake::DIRECT,    35, TimedWake::NONE,       -1, true,  false, true,  false, 48,
    false, true,  false, 10};
constexpr Profile STICKC_PLUS  = {"StickC+",  {true,  true,  true,  true},  120, true,  9,
    MotionWake::DIRECT,    35, TimedWake::NONE,       -1, true,  false, true,  false, 48,
    true,  true,  false, 10};
constexpr Profile STICKC_PLUS2 = {"StickC+2", {true,  true,  false, false}, 200, false, 19,
    MotionWake::NONE,      -1, TimedWake::RTC_ALARM,  4,  false, false, true,  false, 32,
    true,  true,  false, 19};
constexpr Profile STICKS3      = {"StickS3",  {true,  true,  false, true},  250, false, 46,
    MotionWake::PMIC_GPIO, 13, TimedWake::PMIC_TIMER, -1, false, false, true,  true,  32,
    true,  false, false, -1};
constexpr Profile CORE         = {"Core",     {true,  false, false, false}, 0,   false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  false, false, 32,
    true,  false, false, -1};
constexpr Profile CORE2        = {"Core2",    {true,  true,  true,  true},  390, true,  -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  true,  false, 32,
    true,  false, true,  -1};
constexpr Profile TOUGH        = {"Tough",    {true,  true,  true,  true},  390, false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, true,  false, true,  false, 32,
    false, false, false, -1};
// An unrecognized board has no trustworthy battery backend. The Waveshare
// ESP32-S3-ETH in particular is a mains/PoE node with no battery or PMIC, so
// M5Unified's fallback values must not become a fake battery percentage.
constexpr Profile UNKNOWN      = {"M5",       {false, false, false, false}, 0,   false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  false, 32,
    false, false, false, -1};
// clang-format on
}  // namespace

const Profile &profileFor(m5::board_t board) {
  switch (board) {
    case m5::board_t::board_M5StickC:
      return STICKC;
    case m5::board_t::board_M5StickCPlus:
      return STICKC_PLUS;
    case m5::board_t::board_M5StickCPlus2:
      return STICKC_PLUS2;
    case m5::board_t::board_M5StickS3:
      return STICKS3;
    case m5::board_t::board_M5Stack:
      return CORE;
    case m5::board_t::board_M5StackCore2:
      return CORE2;
    case m5::board_t::board_M5Tough:
      return TOUGH;
    default:
      return UNKNOWN;
  }
}

const Profile &profile(void) {
  return profileFor(M5.getBoard());
}

}  // namespace Board
}  // namespace Furble
