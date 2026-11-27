#include "FurbleBoard.h"

namespace Furble {
namespace Board {

namespace {
// Battery capacities are the vendor nominal figures, not a gauge result:
// StickC 95, Plus 120, Plus2 200, StickS3 250, Core 110 (M5Stack Basic page,
// "Battery 110 mAh @ 3.7 V", changed from 150 mAh in 2020.3), Core2 500
// (M5Stack Core2 page, changed from 390 mAh in 2023.10) and Tough 390
// (M5Tough README). The Core's IP5306 gives a coarse four-level percentage
// only; that limits the level, not the capacity. Battery caps still follow
// the PMIC: AXP192 boards report all four, the M5PM1 has no battery current
// register, the Plus2 reads an ADC divider and the Core's IP5306 gives a
// coarse level plus a charge-state bit, with no voltage or current ADC.
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
// EXT_5V rail: M5Unified 0.2.20 Power_Class::setExtOutput() switches the external
// 5 V output: AXP192 EXTEN on the StickC and StickC Plus, the M5PM1 on the
// StickS3, and AXP192 GPIO0 enabling the SY7088 boost on the Core2 and Tough.
// A Core2 v1.1 that carries the AXP2101 reaches Axp2101.setBLDO2() from the same
// call, not LDOIO0. It does nothing on the Plus2, the Core or any other board
// furble supports. On the StickC and Plus the rail is already on before the GPS
// driver runs, by two writes during M5.begin(): M5Unified calls
// Power.setExtOutput(cfg.output_power) with the default output_power = true
// (M5Unified.cpp), which reaches Axp192.setEXTEN(true) for that board pair, and
// M5GFX 0.2.27 Light_M5StickC::init() writes AXP192 0x12 = 0x4D. Bit 6 of that
// register is EXTEN (M5Unified AXP192_Class::setEXTEN()).
//
// The rail column is named for the port, not for one client: on the StickS3
// the same switch feeds the Grove 5 V output, the HAT EXT_5V pin, and the
// infrared transmitter and receiver, and the rail manager shares it between
// holders (include/FurbleRail.h). The M5StickS3 vendor page states that
// EXT_5V_EN feeds those three outputs. It also claims M5Unified leaves the port
// disabled at init, which the pinned 0.2.20 does not: output_power defaults to
// true and _begin() sets the bit, so Platform::init()'s output_power = false is
// load bearing, not belt and braces.
//
// The EXTEN-to-Grove-5V routing is documented by the vendor pinmap: the
// M5StickC and M5StickC Plus AXP192 output tables both list a 5V GROVE output
// beside the LCD, backlight and MCU rails. That EXTEN is bit 6 of register 0x12
// comes from the AXP192 register description and M5Unified, not from a
// schematic in this tree.
//
// On the Core2 and Tough that boost is one BUS_5V net: the Tough schematic ties
// the SY7088 U8 output to Grove Port A pin 2 (J4) and to the M-Bus 5 V pin
// (J7), so Rail::GPS is the whole external 5 V net, not a private GPS rail, and
// a GPS power cycle also cuts anything else on Port A or the M-Bus. The request
// is advisory: M5Unified refuses an enable when there is no ACIN, VBUS current
// is present and the battery is at or below 8 percent, and setRail() returns
// the final readback so the GPS driver can restart a receiver it stopped.
//
// Timed wake: the Plus2 keeps its BM8563 alive across a HOLD power-off so the
// timer can wake the device (plan 320), and on the StickC Plus the same BM8563
// INT reaches the AXP192 power key through R401 and C401, so an AXP192 shutdown
// is its sleep and the alarm is its wake. Both rows use TimedWake::RTC_ALARM and
// keep the early-wake rejection, which reads the countdown registers back. The
// Tough joins them: the Tough schematic fits BT1 BATTERY_RTC on the RTC supply
// and the Tough pinmap wires BM8563 INT to the AXP192 PWR net, so the alarm can
// pull the power key and restart the board after a full PMIC cut. The Core2
// schematic has the same INT net but the 2023.2 revision removed the RTC button
// cell and the firmware cannot detect the revision, so that row stays NONE.
//
// RTC backing: a row is backed when its calendar survives a power-off. The
// Plus2 and the Tough do, by the backup cell the timed wake needs. The Core2 row
// keeps the flag for the pre-2023.2 units that still have a cell; a real outage
// is caught by the BM8563 voltage-low flag, which TimeKeeper reads on a backed
// RTC only (plan 350).
//
// Power off: the IP5306 has no shutdown command, so the Core's manual Off is a
// deferred cut rather than an immediate one. M5Unified's _powerOff(false)
// clears SYS_CTL0 bit 1 and enters deep sleep, and the charger then removes the
// boost once the sleeping load has stayed below 45 mA for the SYS_CTL2 window.
// That is why the keep-on bit is held for the running session and released only
// by that sleep.
//
// LED and IR polarity: the StickC and StickC Plus put the IR emitter anode on
// 3.3 V with the cathode on GPIO9, so light is emitted on a low pin and the RMT
// image is inverted for those rows (StickC Plus schematic, LED1 LED_IR; the
// StickC row follows that page because both boards put the emitter on G9, and
// no StickC-specific schematic was available). Their LEDs are wired the same
// way, which M5Unified's own LED setup agrees with: Power_Class.cpp configures
// the StickC and Plus status LED with invert = true on G10 and the Plus2 LED
// with invert = false on G19. The Plus2 drives an NPN stage from GPIO19 for
// both the red LED and the emitter, so it is active high and its RMT image is
// not inverted. The M5StickS3 pinmap places IR_TX on GPIO46 and IR_RX on GPIO42,
// which the irPin column matches. Which way the StickS3 emitter conducts is not
// in that pinmap, so its row keeps the previous non-inverted image and stays
// UNVERIFIED.
//
// The two polarity columns describe a driven pad. A row with ledPin -1 has no
// LED pad, so its ledActiveLow value is inert and every such row now says false:
// the Core, Core2 and Tough have no LED at all (the Core2's green power
// indicator is not software driven), and the StickS3 status LED is the M5PM1
// PWR_CFG LED_CTRL level, not a GPIO. The unknown-board row keeps the previous
// true because nothing is known about a board furble cannot identify.
//
// Shared image: the Core2 image also serves the M5Tough, which has the same SD
// slot and the same NS4168 speaker path, so both rows declare them. Only the
// LDO3 load differs, and that is a backlight rail on the Tough rather than the
// Core2 vibration motor. The Core has no IR emitter and no RTC chip, and its
// IP5306 has no software off command.

// Columns: name, battery {level, voltage, current, charging}, capacity mAh,
// RTC backed, RTC on the bus, IR pin, motion wake, motion pin, timed wake,
// hold pin, PMIC click streak, SD slot, power policies, power-off path, modem
// sleep, brightness floor, feedback sound, light, vibration, LED pin,
// switchable EXT_5V rail, LED active low, IR mark low, GPS pin source, GPS RX
// pin, GPS TX pin.
//
// RTC on the bus: the part is a BM8563 at 0x51 on the internal I2C bus. The
// vendor pinmap lists one on the StickC, the StickC Plus, the StickC Plus2, the
// Core2 and the Tough, and lists none on the Basic, the StickS3 or the Waveshare
// ESP32-S3-ETH. M5Unified 0.2.x falls back to PCF8563_Class for every board and
// keeps the instance only when its writes at 0x00 and 0x0E are acknowledged, so
// M5.Rtc.isEnabled() is true on exactly those five rows. The AXP192's own RTC
// is not on that bus and is not reachable. The column is separate from the
// backed one: it says a write reaches a chip, not that the chip keeps time.
//
// A row with a GPS UART leaves the pins to M5Unified: its pin table describes
// Port A per SoC target and reports -1 for a board it does not know. A board
// M5Unified cannot describe sets GpsPinSource::PROFILE and carries its own pins
// here. A board whose Port A belongs to another peripheral sets
// GpsPinSource::NONE and claims no pin at all: the Core, where Port A is the
// internal I2C bus, and the Waveshare ESP32-S3-ETH, where it is the W5500 SPI
// clock. See plans/336-gps-pin-seam.md.
//
// clang-format off
constexpr Profile STICKC        = {"StickC", {true,  true,  true,  true}, 95, true, true, 9,
    MotionWake::DIRECT, 35, TimedWake::NONE, -1, true, false, true, true, false, 48, false,
    true, false, 10, true, true, true,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
constexpr Profile STICKC_PLUS   = {"StickC+", {true,  true,  true,  true}, 120, true, true, 9,
    MotionWake::DIRECT, 35, TimedWake::RTC_ALARM, -1, true, false, true, true, false, 48,
    true, true, false, 10, true, true, true,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
constexpr Profile STICKC_PLUS2  = {"StickC+2", {true,  true,  false, false}, 200, true, true, 19,
    MotionWake::NONE, -1, TimedWake::RTC_ALARM, 4, false, false, true, true, false, 32,
    true, true, false, 19, false, false, false,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
constexpr Profile STICKS3       = {"StickS3", {true,  true,  false, true}, 250, false, false, 46,
    MotionWake::PMIC_GPIO, 13, TimedWake::PMIC_TIMER, -1, false, false, true, true, true,
    32, true, false, false, -1, true, false, false,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
constexpr Profile CORE          = {"Core", {true,  false, false, true}, 110, false, false, -1,
    MotionWake::NONE, -1, TimedWake::NONE, -1, false, true, false, true, false, 32, true,
    false, false, -1, false, false, false,
    GpsPinSource::NONE, -1, -1};
constexpr Profile CORE2         = {"Core2", {true,  true,  true,  true}, 500, true, true, -1,
    MotionWake::NONE, -1, TimedWake::NONE, -1, false, true, true, true, false, 32, true,
    false, true, -1, true, false, false,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
constexpr Profile TOUGH         = {"Tough", {true,  true,  true,  true}, 390, true, true, -1,
    MotionWake::NONE, -1, TimedWake::RTC_ALARM, -1, true, true, true, true, false, 32, true,
    false, false, -1, true, false, false,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
// The Waveshare ESP32-S3-ETH is a mains or PoE node. It has no PMIC, no
// battery and no switchable rail, so there is nothing for a software power off
// to switch: with no PMIC, M5Unified's Power_Class::powerOff() arms no wake
// source and ends at esp_deep_sleep_start(), leaving the node asleep until a
// power cycle while the call reports success. The board also has no RTC part,
// no IR emitter, no IMU, no speaker, no vibration motor and no Port A rail. Its
// microSD slot (GPIO 4 to 7) exists, but no card-select pin is mapped for it, so
// the profile reports no SD slot even though the image is built with
// FURBLE_HAS_SD. M5Unified detects the board as board_M5StampS3; see
// plans/349-waveshare-board-row.md.
constexpr Profile WAVESHARE    = {"Waveshare", {false, false, false, false}, 0, false, false, -1,
    MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, false, false, false, 32,
    false, false, false, -1, false, false, false,
    GpsPinSource::NONE, -1, -1};
// An unrecognized board has no trustworthy battery backend. The Waveshare
// ESP32-S3-ETH in particular is a mains/PoE node with no battery or PMIC, so
// M5Unified's fallback values must not become a fake battery percentage.
constexpr Profile UNKNOWN       = {"M5", {false, false, false, false}, 0, false, false, -1,
    MotionWake::NONE, -1, TimedWake::NONE, -1, false, false, true, true, false, 32, false,
    false, false, -1, false, true, false,
    GpsPinSource::BOARD_SUPPORT, -1, -1};
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
    case m5::board_t::board_M5StampS3:
      return WAVESHARE;
    default:
      return UNKNOWN;
  }
}

#if defined(FURBLE_SIM)
// The simulator has no M5 hardware to detect, so a scenario picks the row it
// exercises through this hook. Firmware links the weak default; host tests keep
// it too, and sim/FurblePlatformSim.cpp supplies the strong definition.
extern "C" __attribute__((weak)) bool furble_sim_board_profile(m5::board_t *board) {
  (void)board;
  return false;
}
#endif

const Profile &profile(void) {
#if defined(FURBLE_SIM)
  m5::board_t board = M5.getBoard();
  if (furble_sim_board_profile(&board)) {
    return profileFor(board);
  }
#endif
  return profileFor(M5.getBoard());
}

bool gpsPins(const Profile &row, int8_t &rx, int8_t &tx) {
  rx = -1;
  tx = -1;
  switch (row.gpsPinSource) {
    case GpsPinSource::BOARD_SUPPORT:
      // The one M5Unified pin lookup on the GPS path. Port A is pin_name_t
      // port_a_pin1/port_a_pin2, which alias ex_i2c_scl/ex_i2c_sda, and its
      // per-target table fills them from the row for the detected board or
      // from the board_unknown row. A missing pin is 255 in that uint8_t table
      // and lands in the int8_t copy as -1.
      rx = static_cast<int8_t>(M5.getPin(m5::port_a_pin1));
      tx = static_cast<int8_t>(M5.getPin(m5::port_a_pin2));
      return true;
    case GpsPinSource::PROFILE:
      rx = row.gpsRxPin;
      tx = row.gpsTxPin;
      return true;
    case GpsPinSource::NONE:
      break;
  }
  return false;
}

bool gpsPins(int8_t &rx, int8_t &tx) {
  return gpsPins(profile(), rx, tx);
}

}  // namespace Board
}  // namespace Furble
