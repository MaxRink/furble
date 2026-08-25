#ifndef FURBLE_SIM_PLATFORM_STATE_H
#define FURBLE_SIM_PLATFORM_STATE_H

namespace Furble::Sim {

/** Return the virtual M5PM1 watchdog state for scenario assertions. */
const char *watchdogState(void);

/** Return the virtual M5PM1 long-press download-lock state. */
const char *downloadLockState(void);

}  // namespace Furble::Sim

#endif
