#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <freertos/FreeRTOS.h>

#include "clock.h"
#include "power_profiler.h"

struct FurbleSimQueue {
  FurbleSimQueue(size_t length, size_t itemSize) : capacity {length}, item_size {itemSize} {}

  const size_t capacity;
  const size_t item_size;
  std::deque<std::vector<uint8_t>> items;
  std::mutex mutex;
  std::condition_variable condition;
  FurbleSimQueueResetCallback reset_callback = nullptr;
};

thread_local const char *simTaskName = "ui";

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t,
                       void *parameter,
                       UBaseType_t,
                       TaskHandle_t *task_handle) {
  if (task == nullptr) {
    return pdFALSE;
  }

  std::thread worker([task, parameter, name]() {
    simTaskName = name == nullptr ? "task" : name;
    task(parameter);
  });
  worker.detach();
  if (task_handle != nullptr) {
    *task_handle = reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(1));
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t) {}

void vTaskDelay(TickType_t ticks) {
  if (std::strcmp(simTaskName, "ui") == 0) {
    Furble::Sim::profilerTaskDelay(simTaskName, ticks);
    Furble::Sim::advanceClock(ticks);
    // The UI owns virtual-time advancement. Give detached FreeRTOS task
    // threads a real scheduling point after every tick so they observe that
    // time while a scripted wait is in progress, instead of only after the UI
    // has advanced the entire wait budget. A host yield is only advisory and
    // remained flaky under load; this bounded handoff makes progress reliable
    // without deriving production timing from wall time.
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  } else {
    // Background tasks use this only to yield to the host scheduler. Counting
    // these calls would expose host thread timing instead of scenario time.
    std::this_thread::yield();
  }
}

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size) {
  if (queue_length == 0 || item_size == 0) {
    return nullptr;
  }
  return new FurbleSimQueue(queue_length, item_size);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
  if (queue == nullptr || item == nullptr) {
    return pdFALSE;
  }

  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() >= queue->capacity) {
    return pdFALSE;
  }

  std::vector<uint8_t> value(queue->item_size);
  std::memcpy(value.data(), item, queue->item_size);
  queue->items.push_back(std::move(value));
  queue->condition.notify_one();
  return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait) {
  if (queue == nullptr || item == nullptr) {
    Furble::Sim::profilerQueueReceive("queue", false);
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
    Furble::Sim::profilerQueueReceive("queue", false);
    return pdFALSE;
  }

  const auto value = std::move(queue->items.front());
  queue->items.pop_front();
  std::memcpy(item, value.data(), queue->item_size);
  Furble::Sim::profilerQueueReceive("queue", true);
  return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
  if (queue == nullptr) {
    return pdFALSE;
  }

  FurbleSimQueueResetCallback callback = nullptr;
  {
    std::lock_guard<std::mutex> lock(queue->mutex);
    queue->items.clear();
    callback = queue->reset_callback;
  }
  if (callback != nullptr) {
    callback(queue);
  }
  return pdTRUE;
}

void vQueueDelete(QueueHandle_t queue) {
  delete queue;
}

void furble_sim_queue_set_reset_callback(QueueHandle_t queue,
                                         FurbleSimQueueResetCallback callback) {
  if (queue == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->reset_callback = callback;
}
