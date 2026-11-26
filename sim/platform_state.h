#ifndef FURBLE_SIM_PLATFORM_STATE_H
#define FURBLE_SIM_PLATFORM_STATE_H

#include <cstdint>

#include <M5Unified.h>

namespace Furble::Sim {

/** Settings loaded at the simulated platform construction boundary. */
struct boot_settings_t {
  bool imu;
  uint8_t fb_output;
};

/** Record the boot setting snapshot used by the simulator observation seam. */
void captureBootSettings(const boot_settings_t &settings);

/** Return the setting snapshot recorded during simulated platform startup. */
boot_settings_t bootSettings(void);

/** Return the virtual M5PM1 watchdog state for scenario assertions. */
const char *watchdogState(void);

/** Suppress only the next simulated UI-cycle watchdog feed after a stall. */
void suppressNextWatchdogFeed(void);

/** Consume the one-cycle watchdog-feed suppression requested by a scenario. */
bool consumeWatchdogFeedSuppression(void);

/** Return the virtual M5PM1 long-press download-lock state. */
const char *downloadLockState(void);

/**
 * Whether a latched PMIC interrupt would let a motion edge wake the system.
 *
 * The StickS3 IMU interrupt is M5PM1 GPIO4 and the PMIC aggregates it with the
 * system events onto PYG1_IRQ, which is level active into GPIO13. An unmasked
 * latch in either status register holds that line asserted, so a later motion
 * edge cannot wake anything until a write clears it.
 */
const char *motionWakeState(void);

/** The aggregated PMIC IRQ line: asserted or idle. */
const char *pmicIrqState(void);

/** Both latched status registers as "<status1>,<status2>" in decimal. */
const char *pmicIrqStatus(void);

/** Latch one system interrupt event, M5PM1 IRQ Status 2. */
bool pmicInjectSystemIrq(uint8_t bits);

/** Write 0 to PM1 IRQ Status 2, the only thing that clears those latches. */
bool pmicClearSystemIrq(void);

/** Return the panel-off state the PM1 status LED level models. */
const char *displayOffState(void);

/**
 * Select the board profile row a scenario exercises.
 *
 * M5Unified's host build reports the detected board, so without this a scenario
 * could only reach the row that board maps to. board_unknown keeps the detected
 * board. Board::profile() asks through the weak furble_sim_board_profile hook.
 */
void setBoardProfile(m5::board_t board);

/** The scenario-selected row, or board_unknown for the detected board. */
m5::board_t boardProfile(void);

}  // namespace Furble::Sim

#endif
