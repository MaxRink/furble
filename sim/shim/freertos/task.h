#ifndef FURBLE_SIM_FREERTOS_TASK_H
#define FURBLE_SIM_FREERTOS_TASK_H

#include <cstddef>
#include <cstdint>

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

#endif
