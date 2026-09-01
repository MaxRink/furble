#ifndef FURBLE_CONTROL_E2E_FREERTOS_H
#define FURBLE_CONTROL_E2E_FREERTOS_H

// Minimal FreeRTOS surface for the real-Control end-to-end harness.
//
// The real src/FurbleControl.cpp and lib/furble camera lifecycle run on real
// std::thread-backed tasks and queues here, so the harness exercises the same
// cross-task connect and disconnect races the device sees. Only the symbols the
// real control code touches are provided. This header is host-test only and
// never affects the firmware build.

#include <cstddef>
#include <cstdint>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;

struct FurbleHostQueue;
typedef FurbleHostQueue *QueueHandle_t;

typedef void (*TaskFunction_t)(void *);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define errQUEUE_FULL 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_size,
                       void *parameter,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle);
void vTaskDelete(TaskHandle_t task_handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

// Stop and join every std::thread-backed task before host static destruction.
void furbleHostStopTasks(void);

class FurbleHostTaskScope {
 public:
  FurbleHostTaskScope() = default;
  ~FurbleHostTaskScope() { furbleHostStopTasks(); }

  FurbleHostTaskScope(const FurbleHostTaskScope &) = delete;
  FurbleHostTaskScope &operator=(const FurbleHostTaskScope &) = delete;
};

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReset(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

#endif
