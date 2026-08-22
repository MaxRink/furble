// Host implementation of the small platform surface that src/FurbleControl.cpp
// needs: a std::thread / std::condition_variable model of the FreeRTOS queues
// and tasks, the Platform and Power singletons, and the ble_gap_conn_cancel
// stub. Standalone, with no profiler, clock or SDL dependency.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "freertos/FreeRTOS.h"

#include "FurblePlatform.h"
#include "FurblePower.h"

// --- FreeRTOS queue model ---------------------------------------------------

struct FurbleHostQueue {
  std::mutex mutex;
  std::condition_variable cond;
  std::deque<std::vector<uint8_t>> items;
  size_t item_size = 0;
  size_t max_len = 0;
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  auto *queue = new FurbleHostQueue();
  queue->item_size = static_cast<size_t>(item_size);
  queue->max_len = static_cast<size_t>(length);
  return queue;
}

void vQueueDelete(QueueHandle_t queue) {
  delete queue;
}

static BaseType_t queuePush(QueueHandle_t queue, const void *item, bool front) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::vector<uint8_t> copy(queue->item_size);
  std::memcpy(copy.data(), item, queue->item_size);

  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() >= queue->max_len) {
    return pdFALSE;
  }
  if (front) {
    queue->items.push_front(std::move(copy));
  } else {
    queue->items.push_back(std::move(copy));
  }
  queue->cond.notify_one();
  return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait) {
  (void)ticks_to_wait;  // Control only ever sends with a zero timeout.
  return queuePush(queue, item, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait) {
  (void)ticks_to_wait;
  return queuePush(queue, item, true);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t ticks_to_wait) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::unique_lock<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) {
    if (ticks_to_wait == 0) {
      return pdFALSE;
    }
    const auto wait = std::chrono::milliseconds(static_cast<uint32_t>(ticks_to_wait));
    queue->cond.wait_for(lock, wait, [queue] { return !queue->items.empty(); });
    if (queue->items.empty()) {
      return pdFALSE;
    }
  }
  std::memcpy(buffer, queue->items.front().data(), queue->item_size);
  queue->items.pop_front();
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

// --- FreeRTOS task model ----------------------------------------------------

struct FurbleHostTask {
  std::thread thread;
};

namespace {
std::mutex g_TasksMutex;
std::vector<FurbleHostTask *> g_Tasks;
}  // namespace

BaseType_t xTaskCreate(TaskFunction_t task_code,
                       const char *name,
                       uint32_t stack_depth,
                       void *parameters,
                       UBaseType_t priority,
                       TaskHandle_t *created_task) {
  (void)name;
  (void)stack_depth;
  (void)priority;

  auto *task = new FurbleHostTask();
  task->thread = std::thread([task_code, parameters] { task_code(parameters); });
  {
    std::lock_guard<std::mutex> lock(g_TasksMutex);
    g_Tasks.push_back(task);
  }
  // A per-target task self-deletes and a control task runs for the whole
  // process, so neither is ever joined. Detach so the std::thread destructor is
  // never reached with a joinable thread.
  task->thread.detach();
  if (created_task != nullptr) {
    *created_task = task;
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
  // Production only passes NULL (delete the calling task). Modelled as a no-op:
  // the task function returns immediately after, which ends the std::thread.
  (void)task;
}

void vTaskDelay(TickType_t ticks_to_delay) {
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<uint32_t>(ticks_to_delay)));
}

TickType_t xTaskGetTickCount(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  return static_cast<TickType_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

// ble_gap_conn_cancel() is provided by the mock BLE layer (MockNimBLE.cpp),
// which every target that pulls in this shim also links, so it is not defined
// here to avoid a duplicate symbol.

// --- Platform and Power singletons -----------------------------------------

namespace Furble {

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

Power &Power::getInstance() {
  static Power instance;
  return instance;
}

}  // namespace Furble
