// Host implementation of the small platform surface that src/FurbleControl.cpp
// needs: a std::thread / std::condition_variable model of the FreeRTOS queues
// and tasks, the Platform and Power singletons, and the ble_gap_conn_cancel
// stub. Standalone, with no profiler, clock or SDL dependency.
//
// Task lifetime follows the plan 123 contract, the same one the console and the
// control end-to-end shims use: tasks stay joinable and furbleHostStopTasks()
// stops and joins every one of them before main() returns. Detached tasks used
// to force these suites to end in std::_Exit(), which skips atexit and so skips
// __llvm_profile_write_file, so under coverage neither suite ever wrote its
// counters and both measured nothing (issue #277).

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

// --- Host task shutdown -----------------------------------------------------
//
// See furbleHostStopTasks() in freertos/FreeRTOS.h for what this is for.

namespace {

std::atomic<bool> g_StopTasks {false};

// Thrown to unwind a task thread out of the blocking primitive it is parked in.
// xTaskCreate() catches it at the top of the task function.
struct StopTask {};

// Set on the threads this shim creates, and only on those. The main thread also
// calls into the blocking primitives (Control::disconnect() runs there and
// sleeps in vTaskDelay), and it must never be unwound: it owns the shutdown.
thread_local bool g_OnShimTask = false;

std::mutex g_QueuesMutex;
std::vector<QueueHandle_t> g_Queues;

// Unwind the calling task if shutdown has begun. Every call site is a
// suspension point in the production code, so the stack unwinds through
// ordinary RAII: the shim's own unique_lock releases the queue on the way out,
// and no production lock is held across a blocking primitive.
void stopPoint(void) {
  if (g_OnShimTask && g_StopTasks.load()) {
    throw StopTask {};
  }
}

}  // namespace

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  auto *queue = new FurbleHostQueue();
  queue->item_size = static_cast<size_t>(item_size);
  queue->max_len = static_cast<size_t>(length);
  {
    std::lock_guard<std::mutex> lock(g_QueuesMutex);
    g_Queues.push_back(queue);
  }
  return queue;
}

void vQueueDelete(QueueHandle_t queue) {
  {
    std::lock_guard<std::mutex> lock(g_QueuesMutex);
    g_Queues.erase(std::remove(g_Queues.begin(), g_Queues.end(), queue), g_Queues.end());
  }
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
  stopPoint();
  std::unique_lock<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) {
    if (ticks_to_wait == 0) {
      return pdFALSE;
    }
    const auto wait = std::chrono::milliseconds(static_cast<uint32_t>(ticks_to_wait));
    queue->cond.wait_for(lock, wait,
                         [queue] { return !queue->items.empty() || g_StopTasks.load(); });
    stopPoint();
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

  // A task created after furbleHostStopTasks() has copied the task list is
  // never joined, so it would outlive main() exactly as a detached task did.
  std::lock_guard<std::mutex> lock(g_TasksMutex);
  if (g_StopTasks.load()) {
    return pdFAIL;
  }

  auto *task = new FurbleHostTask();
  // Created under g_TasksMutex, so a concurrent shutdown either sees the task
  // and joins it or is rejected above. Left joinable for that join.
  task->thread = std::thread([task_code, parameters] {
    g_OnShimTask = true;
    try {
      task_code(parameters);
    } catch (const StopTask &) {
      // Host shutdown unwinding a task that is immortal on device.
    }
  });
  g_Tasks.push_back(task);
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
  stopPoint();
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

// --- Host task shutdown -----------------------------------------------------

void furbleHostStopTasks(void) {
  g_StopTasks.store(true);

  // Wake every primitive a task can be parked in. Each wait predicate also
  // reads the stop flag, so a task cannot miss shutdown between the store above
  // and its own wakeup.
  {
    const std::lock_guard<std::mutex> lock(g_QueuesMutex);
    for (auto *queue : g_Queues) {
      queue->cond.notify_all();
    }
  }

  // Take the task list rather than copying it, so nothing holds g_TasksMutex
  // while a thread that may still take it is being joined, and a second call
  // has nothing left to join. Freeing each task here rather than leaving it to
  // the process exit keeps the sanitized suites clean.
  std::vector<FurbleHostTask *> tasks;
  {
    const std::lock_guard<std::mutex> lock(g_TasksMutex);
    tasks.swap(g_Tasks);
  }
  for (auto *task : tasks) {
    if (task->thread.joinable()) {
      task->thread.join();
    }
    delete task;
  }
}
