#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include "clock.h"
#include "power_profiler.h"

struct FurbleSimQueue {
  FurbleSimQueue(size_t length, size_t itemSize) : capacity {length}, item_size {itemSize} {}

  const size_t capacity;
  const size_t item_size;
  std::deque<std::vector<uint8_t>> items;
  bool deleted = false;
  size_t active_users = 0;
  FurbleSimQueueResetCallback reset_callback = nullptr;
};

namespace {

struct SimTaskExit {};

struct SimTask {
  std::thread worker;
  std::atomic<bool> stopping {false};
  bool finished = false;
  bool blocked = false;
};

class QueueUse {
 public:
  explicit QueueUse(FurbleSimQueue *queue) : queue_ {queue} { queue_->active_users++; }
  ~QueueUse() {
    queue_->active_users--;
    Furble::Sim::schedulerCondition().notify_all();
  }

  QueueUse(const QueueUse &) = delete;
  QueueUse &operator=(const QueueUse &) = delete;

 private:
  FurbleSimQueue *queue_;
};

std::vector<std::shared_ptr<SimTask>> tasks;
thread_local SimTask *currentTask = nullptr;
thread_local const char *simTaskName = "ui";

bool taskStopping(void) {
  return currentTask != nullptr && currentTask->stopping.load();
}

void exitStoppedTask(void) {
  if (currentTask != nullptr
      && (currentTask->stopping.load() || Furble::Sim::schedulerStopping())) {
    throw SimTaskExit {};
  }
}

void setTaskBlockedLocked(bool blocked) {
  if (currentTask != nullptr) {
    currentTask->blocked = blocked;
    Furble::Sim::schedulerCondition().notify_all();
  }
}

bool waitUntilLocked(std::unique_lock<std::mutex> &lock,
                     TickType_t ticks,
                     const std::function<bool()> &ready) {
  if (ready() || ticks == 0) {
    return ready();
  }
  if (ticks == portMAX_DELAY) {
    setTaskBlockedLocked(true);
    Furble::Sim::schedulerCondition().wait(
        lock, [&ready]() { return Furble::Sim::schedulerStopping() || taskStopping() || ready(); });
    setTaskBlockedLocked(false);
    return ready();
  }

  const uint32_t deadline = Furble::Sim::clockMillis() + ticks;
  setTaskBlockedLocked(true);
  Furble::Sim::schedulerCondition().wait(lock, [&ready, deadline]() {
    return Furble::Sim::schedulerStopping() || taskStopping()
           || Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(), deadline) || ready();
  });
  setTaskBlockedLocked(false);
  return ready();
}

void markTaskFinished(const std::shared_ptr<SimTask> &task) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  task->finished = true;
  Furble::Sim::schedulerCondition().notify_all();
}

BaseType_t queueSend(QueueHandle_t queue, const void *item, TickType_t ticksToWait, bool front) {
  exitStoppedTask();
  if (queue == nullptr || item == nullptr) {
    return pdFALSE;
  }

  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  QueueUse use {queue};
  const bool available = waitUntilLocked(lock, ticksToWait, [queue]() {
    return queue->deleted || queue->items.size() < queue->capacity;
  });
  exitStoppedTask();
  if (Furble::Sim::schedulerStopping() || queue->deleted || !available) {
    return pdFALSE;
  }

  std::vector<uint8_t> value(queue->item_size);
  std::memcpy(value.data(), item, queue->item_size);
  if (front) {
    queue->items.push_front(std::move(value));
  } else {
    queue->items.push_back(std::move(value));
  }
  Furble::Sim::schedulerCondition().notify_all();
  return pdTRUE;
}

}  // namespace

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t,
                       void *parameter,
                       UBaseType_t,
                       TaskHandle_t *task_handle) {
  if (task == nullptr) {
    return pdFALSE;
  }

  const auto state = std::make_shared<SimTask>();
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (Furble::Sim::schedulerStopping()) {
      return pdFALSE;
    }
    state->worker = std::thread([state, task, parameter, name]() {
      currentTask = state.get();
      simTaskName = name == nullptr ? "task" : name;
      try {
        task(parameter);
      } catch (const SimTaskExit &) {
        // Teardown and vTaskDelete(NULL) use the same cooperative exit path.
      }
      currentTask = nullptr;
      markTaskFinished(state);
    });
    tasks.push_back(state);
  }

  if (task_handle != nullptr) {
    *task_handle = static_cast<TaskHandle_t>(state.get());
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t taskHandle) {
  SimTask *task = taskHandle == nullptr ? currentTask : static_cast<SimTask *>(taskHandle);
  if (task == nullptr) {
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    task->stopping.store(true);
  }
  Furble::Sim::schedulerCondition().notify_all();
  if (task == currentTask) {
    throw SimTaskExit {};
  }
}

void vTaskDelay(TickType_t ticks) {
  exitStoppedTask();
  if (std::strcmp(simTaskName, "ui") == 0 && currentTask == nullptr) {
    Furble::Sim::profilerTaskDelay(simTaskName, ticks);
    Furble::Sim::advanceClock(ticks);
    // Keep a small host scheduling handoff. It does not contribute to virtual
    // time, but lets a task awakened by this clock advance run promptly.
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    return;
  }

  if (ticks == 0) {
    std::this_thread::yield();
    return;
  }

  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  const uint32_t deadline = Furble::Sim::clockMillis() + ticks;
  Furble::Sim::profilerTaskDelay(simTaskName, ticks);
  setTaskBlockedLocked(true);
  Furble::Sim::schedulerCondition().wait(lock, [deadline]() {
    return Furble::Sim::schedulerStopping() || taskStopping()
           || Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(), deadline);
  });
  setTaskBlockedLocked(false);
  exitStoppedTask();
}

TickType_t xTaskGetTickCount(void) {
  return Furble::Sim::clockMillis();
}

void furble_sim_stop_all_tasks(void) {
  std::vector<std::shared_ptr<SimTask>> snapshot;
  Furble::Sim::schedulerStop();
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    snapshot = tasks;
    for (const auto &task : snapshot) {
      task->stopping.store(true);
    }
  }
  Furble::Sim::schedulerCondition().notify_all();

  for (const auto &task : snapshot) {
    if (task->worker.joinable() && task->worker.get_id() != std::this_thread::get_id()) {
      task->worker.join();
    }
  }

  // Task owners can delete their timers while unwinding. Stop and join the
  // remaining timer workers only after those owners have quiesced, so the
  // timer registry cannot hand teardown a pointer concurrently being freed.
  furble_sim_stop_all_timers();

  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  tasks.clear();
}

void furble_sim_reset_tasks(void) {
  furble_sim_stop_all_tasks();
  Furble::Sim::schedulerReset();
}

bool furble_sim_shutdown_requested(void) {
  return Furble::Sim::schedulerStopping();
}

bool furble_sim_task_blocked(TaskHandle_t taskHandle) {
  if (taskHandle == nullptr) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  return static_cast<SimTask *>(taskHandle)->blocked;
}

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size) {
  if (queue_length == 0 || item_size == 0) {
    return nullptr;
  }
  if (Furble::Sim::schedulerStopping()) {
    return nullptr;
  }
  return new FurbleSimQueue(queue_length, item_size);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticksToWait) {
  return queueSend(queue, item, ticksToWait, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticksToWait) {
  return queueSend(queue, item, ticksToWait, true);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticksToWait) {
  exitStoppedTask();
  if (queue == nullptr || item == nullptr) {
    Furble::Sim::profilerQueueReceive("queue", false);
    return pdFALSE;
  }

  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  QueueUse use {queue};
  const bool available = waitUntilLocked(
      lock, ticksToWait, [queue]() { return queue->deleted || !queue->items.empty(); });
  exitStoppedTask();
  if (Furble::Sim::schedulerStopping() || taskStopping() || queue->deleted || !available
      || queue->items.empty()) {
    Furble::Sim::profilerQueueReceive("queue", false);
    return pdFALSE;
  }

  const auto value = std::move(queue->items.front());
  queue->items.pop_front();
  std::memcpy(item, value.data(), queue->item_size);
  Furble::Sim::schedulerCondition().notify_all();
  Furble::Sim::profilerQueueReceive("queue", true);
  return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
  exitStoppedTask();
  if (queue == nullptr) {
    return pdFALSE;
  }

  FurbleSimQueueResetCallback callback = nullptr;
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    QueueUse use {queue};
    exitStoppedTask();
    if (queue->deleted) {
      return pdFALSE;
    }
    queue->items.clear();
    callback = queue->reset_callback;
    Furble::Sim::schedulerCondition().notify_all();
  }
  if (callback != nullptr) {
    callback(queue);
  }
  return pdTRUE;
}

void vQueueDelete(QueueHandle_t queue) {
  if (queue == nullptr) {
    return;
  }
  {
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    queue->deleted = true;
    queue->items.clear();
    queue->reset_callback = nullptr;
    Furble::Sim::schedulerCondition().notify_all();
    Furble::Sim::schedulerCondition().wait(lock, [queue]() { return queue->active_users == 0; });
  }
  delete queue;
}

void furble_sim_queue_set_reset_callback(QueueHandle_t queue,
                                         FurbleSimQueueResetCallback callback) {
  if (queue == nullptr) {
    return;
  }
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  QueueUse use {queue};
  if (!queue->deleted) {
    queue->reset_callback = callback;
  }
}
