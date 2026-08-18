#ifndef FURBLE_SIM_POWER_PROFILER_H
#define FURBLE_SIM_POWER_PROFILER_H

#include <cstdint>

namespace Furble::Sim {

/** Start a deterministic report window at the current simulator time. */
void profilerBegin(const char *scenario);

/** Record one invocation of a named LVGL timer callback. */
void profilerTimerFire(const char *name);

/** Record the area invalidated by LVGL. */
void profilerInvalidatedArea(uint64_t pixels);

/** Record the number of pixels sent through the display flush path. */
void profilerFlushedPixels(uint64_t pixels);

/** Mark the start and end of one UI scheduler pass. */
void profilerBeginUiCycle(void);
void profilerEndUiCycle(void);

/** Record activity from the FreeRTOS shim. */
void profilerQueueReceive(const char *queue_name, bool returned_data);
void profilerTaskDelay(const char *task_name, uint32_t milliseconds);

/** Record modeled display, radio and GPS state transitions. */
void profilerSetDisplayState(const char *state);
void profilerSetRadioConnected(bool connected);
void profilerRadioEvent(const char *name);
void profilerSetGpsState(const char *state);

/** Record the configured DFS policy and PM lock transitions. */
void profilerPowerConfig(int max_frequency_mhz, int min_frequency_mhz, bool light_sleep_enabled);
void profilerPowerLockAcquire(int lock_type, const char *lock_name, const char *owner);
void profilerPowerLockRelease(int lock_type, const char *lock_name, const char *owner);

/** Write the current report and reset its counters at the same virtual time. */
void profilerWriteReport(const char *path, const char *scenario);
void profilerResetWindow(void);

}  // namespace Furble::Sim

#endif
