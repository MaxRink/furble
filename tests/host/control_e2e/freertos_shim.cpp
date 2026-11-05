// Real-thread FreeRTOS shim for the control end-to-end harness.
//
// Tasks are joinable std::threads and queues are mutex + condition_variable
// backed deques, so the real Control task, the per-target tasks and the
// scenario thread all run concurrently, exactly like the device. Time is real
// wall-clock time: the harness asserts real elapsed bounds, so a stall in the
// production teardown shows up as a real delay here. The harness stops and
// joins the firmware-lifetime tasks before C++ static destruction.

#include <algorithm>
#include <atomic>
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
std::atomic<bool> g_StopTasks {false};
std::mutex g_TasksMutex;
std::vector<std::thread> g_Tasks;
std::atomic<size_t> g_TaskCreateCount {0};
std::atomic<uint32_t> g_LastTaskStackBytes {0};
FurbleHostDelayHook g_DelayHook = nullptr;
std::mutex g_QueuesMutex;
std::vector<FurbleHostQueue *> g_Queues;

struct StopTask {};
}  // namespace

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_size,
                       void *parameter,
                       UBaseType_t,
                       TaskHandle_t *task_handle) {
  (void)name;
  if (task == nullptr) {
    return pdFALSE;
  }

  std::lock_guard<std::mutex> lock(g_TasksMutex);
  if (g_StopTasks.load()) {
    return pdFALSE;
  }
  g_TaskCreateCount.fetch_add(1);
  g_LastTaskStackBytes.store(stack_size);
  g_Tasks.emplace_back([task, parameter]() {
    try {
      task(parameter);
    } catch (const StopTask &) {
      // Host shutdown interrupts firmware tasks that are immortal on-device.
    }
  });
  if (task_handle != nullptr) {
    *task_handle = reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(1));
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t) {}

void vTaskDelay(TickType_t ticks) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
  if (g_DelayHook != nullptr) {
    g_DelayHook();
  }
  if (g_StopTasks.load()) {
    throw StopTask {};
  }
}

void furbleHostSetDelayHook(FurbleHostDelayHook hook) {
  g_DelayHook = hook;
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
  auto *queue = new FurbleHostQueue(queue_length, item_size);
  {
    const std::lock_guard<std::mutex> lock(g_QueuesMutex);
    g_Queues.push_back(queue);
  }
  return queue;
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
  if (g_StopTasks.load()) {
    throw StopTask {};
  }
  if (queue->items.empty()) {
    if (ticks_to_wait == 0) {
      return pdFALSE;
    }
    queue->condition.wait_for(lock, std::chrono::milliseconds(ticks_to_wait),
                              [queue]() { return g_StopTasks.load() || !queue->items.empty(); });
  }

  if (g_StopTasks.load()) {
    throw StopTask {};
  }

  if (queue->items.empty()) {
    return pdFALSE;
  }

  const auto value = std::move(queue->items.front());
  queue->items.pop_front();
  std::memcpy(item, value.data(), queue->item_size);
  return pdTRUE;
}

size_t furbleHostTaskCreateCount(void) {
  return g_TaskCreateCount.load();
}

uint32_t furbleHostLastTaskStackBytes(void) {
  return g_LastTaskStackBytes.load();
}

void furbleHostStopTasks(void) {
  g_StopTasks.store(true);

  // Wake every queue wait immediately. The predicate also checks the stop
  // flag, so a task cannot miss shutdown between the notify and its wakeup.
  // Take each queue mutex before notifying: a waiter that has set the stop
  // flag but not yet entered wait_for can otherwise miss the notification and
  // sleep for portMAX_DELAY.
  {
    const std::lock_guard<std::mutex> lock(g_QueuesMutex);
    for (auto *queue : g_Queues) {
      const std::lock_guard<std::mutex> queueLock(queue->mutex);
      queue->condition.notify_all();
    }
  }

  std::vector<std::thread> tasks;
  {
    const std::lock_guard<std::mutex> lock(g_TasksMutex);
    tasks.swap(g_Tasks);
  }
  for (auto &task : tasks) {
    if (task.joinable()) {
      task.join();
    }
  }
}

BaseType_t xQueueReset(QueueHandle_t queue) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->items.clear();
  return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  if (queue == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(queue->mutex);
  return static_cast<UBaseType_t>(queue->items.size());
}

void vQueueDelete(QueueHandle_t queue) {
  if (queue == nullptr) {
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(g_QueuesMutex);
    const auto it = std::find(g_Queues.begin(), g_Queues.end(), queue);
    if (it != g_Queues.end()) {
      g_Queues.erase(it);
    }
  }
  delete queue;
}

// Binary semaphores for drivers that wait on a writer task. The simulator's
// virtual scheduler is not used here: these are real host semaphores with the
// same take/give contract as FreeRTOS.
struct FurbleHostSemaphore {
  std::mutex mutex;
  std::condition_variable condition;
  bool available = false;
};

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  return new FurbleHostSemaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait) {
  if (semaphore == nullptr) {
    return pdFALSE;
  }
  std::unique_lock<std::mutex> lock(semaphore->mutex);
  if (semaphore->available) {
    semaphore->available = false;
    return pdTRUE;
  }
  if (ticks_to_wait == 0) {
    return pdFALSE;
  }
  const auto ready = semaphore->condition.wait_for(
      lock, std::chrono::milliseconds(ticks_to_wait),
      [semaphore] { return g_StopTasks.load() || semaphore->available; });
  if (g_StopTasks.load()) {
    throw StopTask {};
  }
  if (!ready || !semaphore->available) {
    return pdFALSE;
  }
  semaphore->available = false;
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  if (semaphore == nullptr) {
    return pdFALSE;
  }
  {
    const std::lock_guard<std::mutex> lock(semaphore->mutex);
    semaphore->available = true;
  }
  semaphore->condition.notify_one();
  return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
  delete semaphore;
}
