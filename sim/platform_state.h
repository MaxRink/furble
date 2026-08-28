#ifndef FURBLE_SIM_PLATFORM_STATE_H
#define FURBLE_SIM_PLATFORM_STATE_H

namespace Furble::Sim {

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
