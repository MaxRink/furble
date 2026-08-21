// Real-thread FreeRTOS shim for the control end-to-end harness.
//
// Tasks are detached std::threads and queues are mutex + condition_variable
// backed deques, so the real Control task, the per-target tasks and the
// scenario thread all run concurrently, exactly like the device. Time is real
// wall-clock time: the harness asserts real elapsed bounds, so a stall in the
// production teardown shows up as a real delay here.

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "freertos/FreeRTOS.h"

struct FurbleHostQueue {
  FurbleHostQueue(size_t length, size_t itemSize) : capacity {length}, item_size {itemSize} {}

  const size_t capacity;
  const size_t item_size;
  std::deque<std::vector<uint8_t>> items;
  std::mutex mutex;
  std::condition_variable condition;
};

namespace {
std::chrono::steady_clock::time_point g_Start = std::chrono::steady_clock::now();
}

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t,
                       void *parameter,
                       UBaseType_t,
                       TaskHandle_t *task_handle) {
  (void)name;
  if (task == nullptr) {
    return pdFALSE;
  }

  std::thread worker([task, parameter]() { task(parameter); });
  worker.detach();
  if (task_handle != nullptr) {
    *task_handle = reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(1));
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t) {}

void vTaskDelay(TickType_t ticks) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

TickType_t xTaskGetTickCount(void) {
  const auto now = std::chrono::steady_clock::now();
  return static_cast<TickType_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now - g_Start).count());
}

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size) {
  if (queue_length == 0 || item_size == 0) {
    return nullptr;
  }
  return new FurbleHostQueue(queue_length, item_size);
}

static BaseType_t queueSend(QueueHandle_t queue, const void *item, bool front) {
  if (queue == nullptr || item == nullptr) {
    return pdFALSE;
  }

  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() >= queue->capacity) {
    return pdFALSE;
  }

  std::vector<uint8_t> value(queue->item_size);
  std::memcpy(value.data(), item, queue->item_size);
  if (front) {
    queue->items.push_front(std::move(value));
  } else {
    queue->items.push_back(std::move(value));
  }
  queue->condition.notify_one();
  return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
  return queueSend(queue, item, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t) {
  return queueSend(queue, item, true);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait) {
  if (queue == nullptr || item == nullptr) {
    return pdFALSE;
  }

  std::unique_lock<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) {
    if (ticks_to_wait == 0) {
      return pdFALSE;
    }
    queue->condition.wait_for(lock, std::chrono::milliseconds(ticks_to_wait),
                              [queue]() { return !queue->items.empty(); });
  }

  if (queue->items.empty()) {
    return pdFALSE;
  }

  const auto value = std::move(queue->items.front());
  queue->items.pop_front();
  std::memcpy(item, value.data(), queue->item_size);
  return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->items.clear();
  return pdTRUE;
}

void vQueueDelete(QueueHandle_t queue) {
  delete queue;
}
