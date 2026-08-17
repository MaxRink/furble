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

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_size,
                       void *parameter,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle);
void vTaskDelete(TaskHandle_t task_handle);
void vTaskDelay(TickType_t ticks);

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

typedef void (*FurbleSimQueueResetCallback)(QueueHandle_t queue);
void furble_sim_queue_set_reset_callback(QueueHandle_t queue, FurbleSimQueueResetCallback callback);

#endif
