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
  RTC_ALARM,   // BM8563 alarm, then the hold pin is released
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
 */
struct Profile {
  const char *name;  // boot info block
  PowerBackend::battery_caps_t battery;
  uint16_t batteryCapacityMah;  // 0 when unknown
  bool rtcBatteryBacked;        // the RTC keeps time with the main cell out
  int8_t irPin;                 // -1 when the board has no IR emitter
  MotionWake motionWake;
  int8_t motionWakePin;  // SoC pin for motionWake, -1 for NONE
  TimedWake timedWake;
  int8_t powerHoldPin;    // driven high to stay on, -1 when none
  bool pmicClickStreak;   // emulate a click streak from the PMIC key
  bool sdSlot;            // an SD slot on the display SPI bus
  bool canPowerOff;       // a software power-off path exists
  bool modemSleep;        // the controller is configured for modem sleep
  uint8_t minBrightness;  // panel floor, a multiple of the slider step
  bool feedbackSound;
  bool feedbackLight;
  bool feedbackVibration;
  int8_t ledPin;  // -1 when none
};

/** The row for a board model. Unknown boards get a no-hardware row. */
const Profile &profileFor(m5::board_t board);

/** The row for the running board. Valid after Platform::init(). */
const Profile &profile(void);

}  // namespace Board
}  // namespace Furble

#endif
