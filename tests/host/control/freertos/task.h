// Host FreeRTOS task shim. See freertos/FreeRTOS.h for the rationale.
#ifndef INC_TASK_H
#define INC_TASK_H

#include "freertos/FreeRTOS.h"

struct FurbleHostTask;
typedef struct FurbleHostTask *TaskHandle_t;

typedef void (*TaskFunction_t)(void *);

// Starts task_code on a real std::thread so a per-target task actually runs its
// command loop, dequeues CMD_DISCONNECT and sets m_Stopped, exactly as on the
// device.
BaseType_t xTaskCreate(TaskFunction_t task_code,
                       const char *name,
                       uint32_t stack_depth,
                       void *parameters,
                       UBaseType_t priority,
                       TaskHandle_t *created_task);

// The production code only ever passes NULL (delete the calling task). Modelled
// as a no-op: the task function returns straight after, which ends the thread.
void vTaskDelete(TaskHandle_t task);

void vTaskDelay(TickType_t ticks_to_delay);

TickType_t xTaskGetTickCount(void);

#endif
