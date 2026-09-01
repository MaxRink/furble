// Host FreeRTOS task shim. See freertos/FreeRTOS.h for the rationale.
#ifndef INC_TASK_H
#define INC_TASK_H

#include "freertos/FreeRTOS.h"

struct FurbleHostTask;
typedef struct FurbleHostTask *TaskHandle_t;

typedef void (*TaskFunction_t)(void *);

// The 'perf tasks' command samples this twice a second apart and reports the
// per task run time delta. The shim keeps one entry per created task plus the
// caller, which is enough to drive the whole reporting loop and the overflow
// guard.
typedef struct {
  TaskHandle_t xHandle;
  const char *pcTaskName;
  UBaseType_t xTaskNumber;
  UBaseType_t uxCurrentPriority;
  uint32_t ulRunTimeCounter;
  uint32_t usStackHighWaterMark;
} TaskStatus_t;

BaseType_t xTaskCreate(TaskFunction_t task_code,
                       const char *name,
                       uint32_t stack_depth,
                       void *parameters,
                       UBaseType_t priority,
                       TaskHandle_t *created_task);

void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks_to_delay);
TickType_t xTaskGetTickCount(void);

UBaseType_t uxTaskGetSystemState(TaskStatus_t *array,
                                 UBaseType_t array_size,
                                 uint32_t *total_run_time);
UBaseType_t uxTaskGetNumberOfTasks(void);

#endif
