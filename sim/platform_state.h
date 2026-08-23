#ifndef FURBLE_SIM_PLATFORM_STATE_H
#define FURBLE_SIM_PLATFORM_STATE_H

namespace Furble::Sim {

/** Return the virtual M5PM1 watchdog state for scenario assertions. */
const char *watchdogState(void);

}  // namespace Furble::Sim

#endif
