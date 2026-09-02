#ifndef FURBLE_SIM_FREERTOS_TASK_H
#define FURBLE_SIM_FREERTOS_TASK_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "FreeRTOS.h"

enum FurbleSimTaskLifecycle : uint8_t {
  FURBLE_SIM_TASK_RUNNING,
  FURBLE_SIM_TASK_STOP_REQUESTED,
  FURBLE_SIM_TASK_FINISHED,
  FURBLE_SIM_TASK_JOINING,
  FURBLE_SIM_TASK_JOINED,
};

/** Read a retained simulator task lifecycle while taking the scheduler lock. */
FurbleSimTaskLifecycle furble_sim_task_lifecycle(TaskHandle_t task_handle);

/** Return the number of callers waiting for this task's join claimant. */
size_t furble_sim_task_join_waiters(TaskHandle_t task_handle);

/** Return whether a caller has claimed this task's join. */
bool furble_sim_task_join_claimed(TaskHandle_t task_handle);

/** Return the FreeRTOS priority supplied when this task was created. */
UBaseType_t furble_sim_task_priority(TaskHandle_t task_handle);

/** Return the deterministic creation order assigned by the simulator. */
uint64_t furble_sim_task_creation_order(TaskHandle_t task_handle);

/**
 * Return the scheduler's monotonic progress counter. Every dispatch, block,
 * release and boundary entry bumps it, so an unchanged value means nothing in
 * the scheduler moved. The stall watchdog samples it without taking the
 * scheduler lock, which a stalled run may never release.
 */
uint64_t furble_sim_scheduler_progress(void);

/**
 * Append one line per retained task describing its scheduler state. Used by
 * the stall watchdog, so it takes the scheduler lock only if it is free and
 * says so plainly when it is not.
 */
void furble_sim_report_tasks(std::string &out);

#endif
