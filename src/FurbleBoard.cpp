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
// Motion wake: the StickC family wake pin is GPIO35, one open-drain net with
// three sources: the MPU6886 INT, the BM8563 INT (StickC and StickC Plus), and
// the AXP192 IRQ output (both), which M5Unified leaves enabled for the power
// key (reg 0x42 = 0x03). A low level therefore means "motion or a power-key
// event", and Platform treats it as either (plan 350). The StickS3 BMI270 INT1
// goes to M5PM1 GPIO4, and the PMIC drives PYG1_IRQ into GPIO13.
//
// GPS rail: M5Unified 0.2.20 Power_Class::setExtOutput() switches Port A 5 V
// through AXP192 EXTEN on the StickC and StickC Plus, the M5PM1 on the
// StickS3, and AXP192 GPIO0 or AXP2101 BLDO2 on the Core2 and Tough. It does
// nothing on the Plus2, the Core or any other board furble supports. On the
// StickC and Plus the rail is already on before the GPS driver runs: M5GFX
// 0.2.27 Light_M5StickC::init() writes AXP192 0x12 = 0x4D during M5.begin(),
// and bit 6 of that register is EXTEN (M5Unified AXP192_Class::setEXTEN()).
// The EXTEN-to-Grove-5V routing itself is UNVERIFIED: the schematics were not
// available in this pass (plan 350).
//
// Timed wake: the Plus2 keeps its BM8563 alive across a HOLD power-off so the
// timer can wake the device (plan 320), and on the StickC Plus the same BM8563
// INT reaches the AXP192 power key through R401 and C401, so an AXP192 shutdown
// is its sleep and the alarm is its wake. Both rows use TimedWake::RTC_ALARM and
// keep the early-wake rejection, which reads the countdown registers back.
//
// RTC backing: the Plus2 row is battery backed for the same reason: its
// calendar survives the HOLD power-off that the timed wake uses. A real outage
// is caught by the BM8563 voltage-low flag, which TimeKeeper reads on a backed
// RTC only (plan 350).
//
// LED and IR polarity: the StickC and StickC Plus put the IR emitter anode on
// 3.3 V with the cathode on GPIO9, so light is emitted on a low pin and the RMT
// image is inverted for those rows (StickC Plus schematic, LED1 LED_IR). Their
// LEDs are wired the same way. The Plus2 drives an NPN stage from GPIO19 for
// both the red LED and the emitter, so it is active high and its RMT image is
// not inverted. The StickS3 emitter wiring was not available in this pass, so
// its row keeps the previous non-inverted image and is UNVERIFIED.

// Columns: name, battery {level, voltage, current, charging}, capacity mAh,
// RTC backed, IR pin, motion wake, motion pin, timed wake, hold pin, PMIC
// click streak, SD slot, can power off, modem sleep, brightness floor,
// feedback sound, light, vibration, LED pin, GPS rail, LED active low, IR mark
// low.
//
// clang-format off
constexpr Profile STICKC       = {"StickC",   {true,  true,  true,  true},  95,  true,  9,
    MotionWake::DIRECT,    35, TimedWake::NONE,       -1, true,  false, true,  false, 48,
    false, true,  false, 10, true,  true,  true};
constexpr Profile STICKC_PLUS  = {"StickC+",  {true,  true,  true,  true},  120, true,  9,
    MotionWake::DIRECT,    35, TimedWake::RTC_ALARM,  -1, true,  false, true,  false, 48,
    true,  true,  false, 10, true,  true,  true};
constexpr Profile STICKC_PLUS2 = {"StickC+2", {true,  true,  false, false}, 200, true,  19,
    MotionWake::NONE,      -1, TimedWake::RTC_ALARM,  4,  false, false, true,  false, 32,
    true,  true,  false, 19, false, false, false};
constexpr Profile STICKS3      = {"StickS3",  {true,  true,  false, true},  250, false, 46,
    MotionWake::PMIC_GPIO, 13, TimedWake::PMIC_TIMER, -1, false, false, true,  true,  32,
    true,  false, false, -1, true,  true,  false};
constexpr Profile CORE         = {"Core",     {true,  false, false, false}, 0,   false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  false, false, 32,
    true,  false, false, -1, false, true,  false};
constexpr Profile CORE2        = {"Core2",    {true,  true,  true,  true},  390, true,  -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, true,  true,  false, 32,
    true,  false, true,  -1, true,  true,  false};
constexpr Profile TOUGH        = {"Tough",    {true,  true,  true,  true},  390, false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, true,  false, true,  false, 32,
    false, false, false, -1, true,  true,  false};
// An unrecognized board has no trustworthy battery backend. The Waveshare
// ESP32-S3-ETH in particular is a mains/PoE node with no battery or PMIC, so
// M5Unified's fallback values must not become a fake battery percentage.
constexpr Profile UNKNOWN      = {"M5",       {false, false, false, false}, 0,   false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  false, 32,
    false, false, false, -1, false, true,  false};
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
