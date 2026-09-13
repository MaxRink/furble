#ifndef FURBLE_SIM_PLATFORM_STATE_H
#define FURBLE_SIM_PLATFORM_STATE_H

#include <cstdint>

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

}  // namespace Furble::Sim

#endif
