#ifndef FURBLE_SIM_CRASH_LOG_H
#define FURBLE_SIM_CRASH_LOG_H

#include <cstdint>

namespace Furble::Sim {

/**
 * Write a synthetic previous-boot crash record. The next simulator boot prints
 * it once, the same way the device prints its RTC record after a panic reset.
 * A record is only persisted when FURBLE_SIM_PREFS names the run's flash file.
 */
void crashLogInject(uint32_t resetReason, const char *task, const char *line);

/** True while an injected record has not been printed by the next boot. */
bool crashLogInjected(void);

/** Remove the sidecar record next to the current preferences path. */
void crashLogRemovePersisted(void);

}  // namespace Furble::Sim

#endif
