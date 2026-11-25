#ifndef FURBLE_BOARD_H
#define FURBLE_BOARD_H

#include <cstdint>

#include <M5Unified.h>

#include "FurblePowerBackend.h"

namespace Furble {
namespace Board {

/** How the board powers itself back on after a timed shutdown. */
enum class TimedWake : uint8_t {
  NONE,
  PMIC_TIMER,  // M5PM1 timer plus RTC RAM marker
  RTC_ALARM,   // BM8563 timer; the hold pin is released or the PMIC is cut
};

/** How the IMU interrupt reaches the SoC. */
enum class MotionWake : uint8_t {
  NONE,
  DIRECT,     // IMU INT on a SoC GPIO
  PMIC_GPIO,  // IMU INT on M5PM1 GPIO4, PMIC IRQ on a SoC GPIO
};

/**
 * Runtime facts about one board model.
 *
 * Firmware images serve several boards (the StickC Plus image also boots on a
 * Plus2, the Core2 image on a Tough), so the row is chosen from the detected
 * board, not from a build macro. Compile-time capabilities live in
 * FurbleBoardCaps.h. UI geometry stays in the UI.
 *
 * `canPowerOff` and `powerOffPath` answer different questions and are not the
 * same column. `canPowerOff` is the UI policy gate: it hides the Auto off and
 * Low battery rollers and stops their timers, and it is false on the Core where
 * the profile reports a coarse level and no charging state for those policies
 * to act on. The Core kept that false out of doubt about its IP5306 power-off
 * path; that question moved to `powerOffPath`, which the Core keeps true
 * because M5Unified clears the IP5306 boost keep-on and the power-off scenarios
 * exercise it. The Waveshare ESP32-S3-ETH is the only row without a path: no
 * PMIC, no cell and no rail, so the call would deep sleep the chip with no wake
 * source armed rather than remove power.
 */
struct Profile {
  const char *name;  // boot info block
  PowerBackend::battery_caps_t battery;
  uint16_t batteryCapacityMah;  // vendor nominal, 0 when unknown
  // True when the board is expected to keep its calendar across a power-off, so
  // the RTC is the retention mechanism: a cell, or a rail the board keeps
  // alive. The chip's voltage-low flag stays the guard for a calendar that did
  // stop, and it is read on a backed row only.
  bool rtcBatteryBacked;
  int8_t irPin;  // -1 when the board has no IR emitter
  MotionWake motionWake;
  int8_t motionWakePin;  // SoC pin for motionWake, -1 for NONE
  TimedWake timedWake;
  int8_t powerHoldPin;    // driven high to stay on, -1 when none
  bool pmicClickStreak;   // emulate a click streak from the PMIC key
  bool sdSlot;            // an SD slot on the display SPI bus
  bool canPowerOff;       // the UI offers the Auto off and Low battery policies
  bool powerOffPath;      // the platform can cut power; see Platform::powerOff()
  bool modemSleep;        // the controller is configured for modem sleep
  uint8_t minBrightness;  // panel floor, a multiple of the slider step
  bool feedbackSound;
  bool feedbackLight;
  bool feedbackVibration;
  int8_t ledPin;  // -1 when none
  // M5.Power switches the external 5 V output. On the Core2 and Tough that is
  // one BUS_5V net shared by Grove Port A and the M-Bus, so Rail::GPS is the
  // whole external 5 V net. The request is advisory: the PMIC can refuse it.
  bool gpsRail;
  // The two columns below describe driven stages: board facts, not UI
  // geometry. They are the wiring of the LED and emitter, which is why they
  // cannot be derived from the pin number.
  bool ledActiveLow;  // the LED lights on a low pin
  bool irMarkLow;     // the emitter lights on a low pin, so the RMT output is
                      // inverted to keep the optical frame the right way up
};

/** The row for a board model. Unknown boards get a no-hardware row. */
const Profile &profileFor(m5::board_t board);

/** The row for the running board. Valid after Platform::init(). */
const Profile &profile(void);

}  // namespace Board
}  // namespace Furble

#endif
