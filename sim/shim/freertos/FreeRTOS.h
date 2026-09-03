#ifndef FURBLE_SIM_FREERTOS_H
#define FURBLE_SIM_FREERTOS_H

#include <cstddef>
#include <cstdint>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;

struct FurbleSimQueue;
typedef FurbleSimQueue *QueueHandle_t;

typedef void (*TaskFunction_t)(void *);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define errQUEUE_FULL 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define ESP_INTR_FLAG_IRAM 0

// ESP-IDF 5.5.3 derives the esp_timer task priority from the configured
// FreeRTOS priority range. Keep the host shim on the same production value.
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES 25
#endif
#ifndef ESP_TASK_TIMER_PRIO
#define ESP_TASK_TIMER_PRIO (configMAX_PRIORITIES - 3)
#endif

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_size,
                       void *parameter,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle);
void vTaskDelete(TaskHandle_t task_handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

/** Stop and join every simulator task before firmware objects are destroyed. */
void furble_sim_stop_all_tasks(void);

/** Reset simulator task/scheduler state for an isolated host test run. */
void furble_sim_reset_tasks(void);

/** Return whether simulator teardown has begun. */
bool furble_sim_shutdown_requested(void);

/** Return whether a simulator task is currently blocked on a delay or queue. */
bool furble_sim_task_blocked(TaskHandle_t task_handle);

/**
 * Block until a simulator task is blocked on a delay or queue, or until
 * timeout_ms of wall clock passes. Returns the state it ends on.
 *
 * A caller that has to know a task has parked on a virtual deadline before it
 * advances the clock must use this rather than polling the query above. A poll
 * loop bounded by an iteration count is a spin budget, not a wait: on a loaded
 * host the budget runs out while the task is simply not scheduled yet, and the
 * clock then moves past a deadline the task has not registered. This waits on
 * the scheduler condition variable, so it costs nothing while the task is
 * descheduled and it returns the moment the task parks.
 */
bool furble_sim_wait_task_blocked(TaskHandle_t task_handle, uint32_t timeout_ms);

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

typedef void (*FurbleSimQueueResetCallback)(QueueHandle_t queue);
void furble_sim_queue_set_reset_callback(QueueHandle_t queue, FurbleSimQueueResetCallback callback);

#endif
