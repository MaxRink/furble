#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock.h"
#include "driver.h"
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

enum class SimTaskLifecycle : uint8_t {
  running,
  stop_requested,
  finished,
  joining,
  joined,
};

enum class SimWaitKind : uint8_t {
  none,
  delay,
  queue_send,
  queue_receive,
};

enum class SimWaitResult : uint8_t {
  none,
  ready,
  timeout,
  cancelled,
};

struct SimTask {
  std::thread worker;
  std::atomic<bool> stopping {false};
  UBaseType_t priority = 0;
  uint64_t creation_order = 0;
  uint64_t ready_order = 0;
  uint64_t wait_order = 0;
  SimTaskLifecycle lifecycle = SimTaskLifecycle::running;
  bool blocked = false;
  bool runnable = false;
  SimWaitKind wait_kind = SimWaitKind::none;
  SimWaitResult wait_result = SimWaitResult::none;
  FurbleSimQueue *wait_queue = nullptr;
  uint32_t wait_deadline = 0;
  bool wait_has_deadline = false;
  bool failed = false;
  std::exception_ptr failure;
  // Callers waiting for the worker to finish and the single join claimant.
  size_t join_waiters = 0;
  bool join_claimed = false;
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

// Keep joined records in the registry so a TaskHandle_t remains an observable,
// inert record after reset. They are reclaimed with the process, not while a
// caller may still retain a simulator handle.
std::vector<std::shared_ptr<SimTask>> tasks;
uint64_t nextTaskOrder = 0;
uint64_t nextReadyOrder = 0;
SimTask *runningTask = nullptr;
SimTask timerServiceTask;
bool timerServiceExecuting = false;
thread_local SimTask *currentTask = nullptr;
thread_local const char *simTaskName = "ui";

void releaseTaskLocked(SimTask &task, SimWaitResult result);

bool taskStopping(void) {
  return currentTask != nullptr && currentTask->stopping.load();
}

std::shared_ptr<SimTask> findTaskLocked(SimTask *task) {
  if (task == nullptr) {
    return nullptr;
  }
  for (const auto &record : tasks) {
    if (record.get() == task) {
      return record;
    }
  }
  return nullptr;
}

void requestTaskStopLocked(const std::shared_ptr<SimTask> &task) {
  task->stopping.store(true);
  if (task->blocked) {
    releaseTaskLocked(*task, SimWaitResult::cancelled);
  }
  task->runnable = false;
  if (runningTask == task.get()) {
    runningTask = nullptr;
  }
  if (task->lifecycle == SimTaskLifecycle::running) {
    task->lifecycle = SimTaskLifecycle::stop_requested;
  }
}

void exitStoppedTask(void) {
  if (currentTask != nullptr
      && (currentTask->stopping.load() || Furble::Sim::schedulerStopping())) {
    throw SimTaskExit {};
  }
}

bool taskReadyForDispatch(const SimTask &task) {
  return task.runnable && !task.blocked && !task.stopping.load()
         && !Furble::Sim::schedulerStopping() && task.lifecycle == SimTaskLifecycle::running;
}

SimTask *nextRunnableTaskLocked(void) {
  SimTask *next = nullptr;
  const auto consider = [&next](SimTask *candidate) {
    if (!taskReadyForDispatch(*candidate)) {
      return;
    }
    if (next == nullptr || candidate->priority > next->priority
        || (candidate->priority == next->priority && candidate->ready_order < next->ready_order)) {
      next = candidate;
    }
  };
  for (const auto &candidate : tasks) {
    consider(candidate.get());
  }
  consider(&timerServiceTask);
  return next;
}

void dispatchNextLocked(void) {
  if (runningTask != nullptr) {
    return;
  }
  runningTask = nextRunnableTaskLocked();
}

void waitForTurnLocked(std::unique_lock<std::mutex> &lock) {
  if (currentTask == nullptr) {
    return;
  }
  dispatchNextLocked();
  Furble::Sim::schedulerCondition().wait(lock, []() {
    if (Furble::Sim::schedulerStopping() || taskStopping()) {
      return true;
    }
    dispatchNextLocked();
    return runningTask == currentTask;
  });
}

void preemptForHigherPriorityLocked(std::unique_lock<std::mutex> &lock) {
  if (currentTask == nullptr || runningTask != currentTask) {
    return;
  }
  SimTask *next = nextRunnableTaskLocked();
  if (next == nullptr || next == currentTask || next->priority <= currentTask->priority) {
    return;
  }
  currentTask->ready_order = nextReadyOrder++;
  runningTask = nullptr;
  dispatchNextLocked();
  Furble::Sim::schedulerCondition().notify_all();
  waitForTurnLocked(lock);
}

void setTaskBlockedLocked(bool blocked) {
  if (currentTask != nullptr) {
    if (blocked) {
      currentTask->blocked = true;
      currentTask->runnable = false;
      if (runningTask == currentTask) {
        runningTask = nullptr;
      }
      currentTask->wait_order = nextReadyOrder++;
      dispatchNextLocked();
    } else if (currentTask->blocked) {
      currentTask->blocked = false;
      currentTask->runnable = true;
      currentTask->ready_order = currentTask->wait_order;
      currentTask->wait_kind = SimWaitKind::none;
      currentTask->wait_result = SimWaitResult::none;
      currentTask->wait_queue = nullptr;
      currentTask->wait_has_deadline = false;
      dispatchNextLocked();
    }
    Furble::Sim::schedulerCondition().notify_all();
  }
}

void setTaskWaitLocked(SimWaitKind kind,
                       FurbleSimQueue *queue,
                       bool hasDeadline,
                       uint32_t deadline) {
  if (currentTask == nullptr) {
    return;
  }
  currentTask->wait_kind = kind;
  currentTask->wait_result = SimWaitResult::none;
  currentTask->wait_queue = queue;
  currentTask->wait_has_deadline = hasDeadline;
  currentTask->wait_deadline = deadline;
}

void releaseTaskLocked(SimTask &task, SimWaitResult result) {
  task.blocked = false;
  task.runnable = true;
  task.ready_order = task.wait_order;
  task.wait_kind = SimWaitKind::none;
  task.wait_result = result;
  task.wait_queue = nullptr;
  task.wait_has_deadline = false;
}

void releaseDueWaitersLocked(void) {
  const uint32_t now = Furble::Sim::clockMillis();
  for (const auto &task : tasks) {
    if (task->blocked && task->wait_has_deadline
        && Furble::Sim::clockDeadlineReached(now, task->wait_deadline)) {
      releaseTaskLocked(*task, SimWaitResult::timeout);
    }
  }
  Furble::Sim::schedulerCondition().notify_all();
}

void releaseQueueWaiterLocked(FurbleSimQueue *queue, SimWaitKind kind) {
  SimTask *selected = nullptr;
  for (const auto &task : tasks) {
    if (!task->blocked || task->wait_queue != queue || task->wait_kind != kind) {
      continue;
    }
    if (selected == nullptr || task->priority > selected->priority
        || (task->priority == selected->priority && task->wait_order < selected->wait_order)) {
      selected = task.get();
    }
  }
  if (selected != nullptr) {
    releaseTaskLocked(*selected, SimWaitResult::ready);
    Furble::Sim::schedulerCondition().notify_all();
  }
}

void releaseAllQueueWaitersLocked(FurbleSimQueue *queue) {
  for (const auto &task : tasks) {
    if (task->blocked && task->wait_queue == queue) {
      releaseTaskLocked(*task, SimWaitResult::cancelled);
    }
  }
  dispatchNextLocked();
  Furble::Sim::schedulerCondition().notify_all();
}

void schedulerClockAdvanced(void) {
  // clock.cpp invokes this while holding schedulerMutex. Releasing every due
  // waiter as one batch makes a same-tick deadline an ordered scheduler event,
  // rather than whichever host thread wins the condition-variable wake race.
  releaseDueWaitersLocked();
  Furble::Sim::schedulerTimerDueChanged(furbleSimNextDueTimerLocked() != nullptr);
}

bool waitUntilLocked(std::unique_lock<std::mutex> &lock,
                     TickType_t ticks,
                     const std::function<bool()> &ready,
                     FurbleSimQueue *queue = nullptr,
                     SimWaitKind waitKind = SimWaitKind::none) {
  if (ready() || ticks == 0) {
    return ready();
  }
  if (ticks == portMAX_DELAY) {
    setTaskWaitLocked(waitKind, queue, false, 0);
    setTaskBlockedLocked(true);
    const bool schedulerWait = currentTask != nullptr && waitKind != SimWaitKind::none;
    Furble::Sim::schedulerCondition().wait(lock, [&ready, schedulerWait]() {
      return Furble::Sim::schedulerStopping() || taskStopping()
             || (schedulerWait ? currentTask->wait_result != SimWaitResult::none : ready());
    });
    SimWaitResult result = SimWaitResult::cancelled;
    if (currentTask == nullptr) {
      result = ready() ? SimWaitResult::ready : SimWaitResult::cancelled;
    } else {
      result = currentTask->wait_result;
      if (result == SimWaitResult::none) {
        result = SimWaitResult::cancelled;
      }
    }
    setTaskBlockedLocked(false);
    waitForTurnLocked(lock);
    if (currentTask != nullptr) {
      currentTask->wait_result = SimWaitResult::none;
    }
    return result == SimWaitResult::ready;
  }

  const uint32_t deadline = Furble::Sim::clockMillis() + ticks;
  setTaskWaitLocked(waitKind, queue, true, deadline);
  setTaskBlockedLocked(true);
  const bool schedulerWait = currentTask != nullptr && waitKind != SimWaitKind::none;
  Furble::Sim::schedulerCondition().wait(lock, [&ready, deadline, schedulerWait]() {
    return Furble::Sim::schedulerStopping() || taskStopping()
           || (schedulerWait
                   ? currentTask->wait_result != SimWaitResult::none
                   : Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(), deadline)
                         || ready());
  });
  SimWaitResult result = SimWaitResult::timeout;
  if (currentTask == nullptr) {
    result = ready() ? SimWaitResult::ready : SimWaitResult::timeout;
  } else {
    result = currentTask->wait_result;
    if (result == SimWaitResult::none) {
      result = SimWaitResult::timeout;
    }
  }
  setTaskBlockedLocked(false);
  waitForTurnLocked(lock);
  if (currentTask != nullptr) {
    currentTask->wait_result = SimWaitResult::none;
  }
  return result == SimWaitResult::ready;
}

void markTaskFinished(const std::shared_ptr<SimTask> &task) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  task->blocked = false;
  task->runnable = false;
  if (runningTask == task.get()) {
    runningTask = nullptr;
  }
  dispatchNextLocked();
  if (task->lifecycle == SimTaskLifecycle::running
      || task->lifecycle == SimTaskLifecycle::stop_requested) {
    task->lifecycle = SimTaskLifecycle::finished;
  }
  Furble::Sim::schedulerCondition().notify_all();
}

void joinTask(const std::shared_ptr<SimTask> &task) {
  std::thread worker;
  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  if (task->worker.joinable() && task->worker.get_id() == std::this_thread::get_id()) {
    return;
  }

  for (;;) {
    if (task->lifecycle == SimTaskLifecycle::joined) {
      return;
    }
    if (task->lifecycle == SimTaskLifecycle::joining) {
      ++task->join_waiters;
      Furble::Sim::schedulerCondition().notify_all();
      Furble::Sim::schedulerCondition().wait(
          lock, [&task]() { return task->lifecycle == SimTaskLifecycle::joined; });
      --task->join_waiters;
      Furble::Sim::schedulerCondition().notify_all();
      return;
    }
    if (task->lifecycle != SimTaskLifecycle::finished) {
      ++task->join_waiters;
      Furble::Sim::schedulerCondition().notify_all();
      Furble::Sim::schedulerCondition().wait(lock, [&task]() {
        return task->lifecycle == SimTaskLifecycle::finished
               || task->lifecycle == SimTaskLifecycle::joining
               || task->lifecycle == SimTaskLifecycle::joined;
      });
      --task->join_waiters;
      Furble::Sim::schedulerCondition().notify_all();
      continue;
    }

    task->lifecycle = SimTaskLifecycle::joining;
    task->join_claimed = true;
    worker = std::move(task->worker);
    break;
  }

  lock.unlock();
  if (worker.joinable()) {
    worker.join();
  }
  lock.lock();
  task->lifecycle = SimTaskLifecycle::joined;
  Furble::Sim::schedulerCondition().notify_all();
}

BaseType_t queueSend(QueueHandle_t queue, const void *item, TickType_t ticksToWait, bool front) {
  exitStoppedTask();
  if (queue == nullptr || item == nullptr) {
    return pdFALSE;
  }

  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  QueueUse use {queue};
  const bool available = waitUntilLocked(
      lock, ticksToWait,
      [queue]() { return queue->deleted || queue->items.size() < queue->capacity; }, queue,
      SimWaitKind::queue_send);
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
  releaseQueueWaiterLocked(queue, SimWaitKind::queue_receive);
  dispatchNextLocked();
  preemptForHigherPriorityLocked(lock);
  Furble::Sim::schedulerCondition().notify_all();
  return pdTRUE;
}

}  // namespace

namespace Furble::Sim {

void schedulerTimerDueChanged(bool due) {
  timerServiceTask.priority = ESP_TASK_TIMER_PRIO;
  if (due) {
    if (!timerServiceTask.runnable) {
      timerServiceTask.runnable = true;
      timerServiceTask.ready_order = nextReadyOrder++;
    }
  } else if (!timerServiceExecuting) {
    timerServiceTask.runnable = false;
    if (runningTask == &timerServiceTask) {
      runningTask = nullptr;
    }
  }
  dispatchNextLocked();
  schedulerCondition().notify_all();
}

void runSchedulerTimerCallback(SchedulerCallback callback, void *argument) {
  if (callback == nullptr) {
    return;
  }

  SimTask *previousTask = currentTask;
  const char *previousName = simTaskName;
  currentTask = &timerServiceTask;
  simTaskName = "timer";

  std::unique_lock<std::mutex> lock(schedulerMutex());
  timerServiceTask.priority = ESP_TASK_TIMER_PRIO;
  if (!timerServiceTask.runnable) {
    timerServiceTask.runnable = true;
    timerServiceTask.ready_order = nextReadyOrder++;
  }
  waitForTurnLocked(lock);
  const bool admitted = !schedulerStopping() && !timerServiceTask.stopping.load();
  timerServiceExecuting = admitted;
  lock.unlock();

  if (admitted) {
    try {
      callback(argument);
    } catch (...) {
      requestFailureExit();
      schedulerStop();
    }
  }

  lock.lock();
  timerServiceExecuting = false;
  timerServiceTask.runnable = furbleSimNextDueTimerLocked() != nullptr;
  timerServiceTask.blocked = false;
  timerServiceTask.wait_kind = SimWaitKind::none;
  timerServiceTask.wait_result = SimWaitResult::none;
  if (runningTask == &timerServiceTask) {
    runningTask = nullptr;
  }
  dispatchNextLocked();
  schedulerCondition().notify_all();
  lock.unlock();

  currentTask = previousTask;
  simTaskName = previousName;
}

}  // namespace Furble::Sim

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t,
                       void *parameter,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle) {
  if (task == nullptr) {
    return pdFALSE;
  }

  const auto state = std::make_shared<SimTask>();
  Furble::Sim::setClockAdvanceHook(schedulerClockAdvanced);
  {
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (Furble::Sim::schedulerStopping()) {
      return pdFALSE;
    }
    state->priority = priority;
    state->creation_order = nextTaskOrder++;
    state->ready_order = nextReadyOrder++;
    // Publish readiness before starting the host worker. The scheduler may
    // therefore make an ordering decision before xTaskCreate returns without
    // depending on when the host thread first acquires schedulerMutex.
    state->runnable = true;
    state->worker = std::thread([state, task, parameter, name]() {
      currentTask = state.get();
      simTaskName = name == nullptr ? "task" : name;
      bool admitted = false;
      {
        std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
        waitForTurnLocked(lock);
        admitted = !Furble::Sim::schedulerStopping() && !state->stopping.load();
      }
      try {
        if (admitted) {
          task(parameter);
        }
      } catch (const SimTaskExit &) {
        // Teardown and vTaskDelete(NULL) use the same cooperative exit path.
      } catch (...) {
        {
          const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
          state->failed = true;
          state->failure = std::current_exception();
        }
        Furble::Sim::requestFailureExit();
        Furble::Sim::schedulerStop();
      }
      currentTask = nullptr;
      markTaskFinished(state);
    });
    tasks.push_back(state);
    preemptForHigherPriorityLocked(lock);
  }

  if (task_handle != nullptr) {
    *task_handle = static_cast<TaskHandle_t>(state.get());
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t taskHandle) {
  SimTask *rawTask = taskHandle == nullptr ? currentTask : static_cast<SimTask *>(taskHandle);
  if (rawTask == nullptr) {
    return;
  }
  std::shared_ptr<SimTask> task;
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    task = findTaskLocked(rawTask);
    if (task == nullptr) {
      return;
    }
    requestTaskStopLocked(task);
  }
  Furble::Sim::schedulerCondition().notify_all();
  if (task.get() == currentTask) {
    throw SimTaskExit {};
  }
  joinTask(task);
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
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (currentTask == nullptr) {
      lock.unlock();
      std::this_thread::yield();
      return;
    }
    // A zero tick delay is a real scheduler yield. Re-queue this task behind
    // all peers at the same priority, while retaining FreeRTOS priority
    // dominance over lower-priority runnable work.
    currentTask->runnable = true;
    currentTask->ready_order = nextReadyOrder++;
    if (runningTask == currentTask) {
      runningTask = nullptr;
    }
    dispatchNextLocked();
    Furble::Sim::schedulerCondition().notify_all();
    waitForTurnLocked(lock);
    return;
  }

  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  const uint32_t deadline = Furble::Sim::clockMillis() + ticks;
  setTaskWaitLocked(SimWaitKind::delay, nullptr, true, deadline);
  Furble::Sim::profilerTaskDelay(simTaskName, ticks);
  setTaskBlockedLocked(true);
  Furble::Sim::schedulerCondition().wait(lock, [deadline]() {
    return Furble::Sim::schedulerStopping() || taskStopping()
           || Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(), deadline);
  });
  setTaskBlockedLocked(false);
  waitForTurnLocked(lock);
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
      requestTaskStopLocked(task);
    }
  }
  Furble::Sim::schedulerCondition().notify_all();

  for (const auto &task : snapshot) {
    joinTask(task);
  }

  // Task owners can delete their timers while unwinding. Stop and join the
  // remaining timer workers only after those owners have quiesced, so the
  // timer registry cannot hand teardown a pointer concurrently being freed.
  furble_sim_stop_all_timers();
}

void furble_sim_reset_tasks(void) {
  furble_sim_stop_all_tasks();
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    runningTask = nullptr;
    nextTaskOrder = 0;
    nextReadyOrder = 0;
    timerServiceTask.priority = ESP_TASK_TIMER_PRIO;
    timerServiceTask.runnable = false;
    timerServiceTask.blocked = false;
    timerServiceTask.stopping.store(false);
    timerServiceExecuting = false;
    timerServiceTask.wait_kind = SimWaitKind::none;
    timerServiceTask.wait_result = SimWaitResult::none;
  }
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
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  return task != nullptr && task->blocked;
}

FurbleSimTaskLifecycle furble_sim_task_lifecycle(TaskHandle_t taskHandle) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  if (task == nullptr) {
    return FURBLE_SIM_TASK_JOINED;
  }
  switch (task->lifecycle) {
    case SimTaskLifecycle::running:
      return FURBLE_SIM_TASK_RUNNING;
    case SimTaskLifecycle::stop_requested:
      return FURBLE_SIM_TASK_STOP_REQUESTED;
    case SimTaskLifecycle::finished:
      return FURBLE_SIM_TASK_FINISHED;
    case SimTaskLifecycle::joining:
      return FURBLE_SIM_TASK_JOINING;
    case SimTaskLifecycle::joined:
      return FURBLE_SIM_TASK_JOINED;
  }
  return FURBLE_SIM_TASK_JOINED;
}

size_t furble_sim_task_join_waiters(TaskHandle_t taskHandle) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  return task == nullptr ? 0 : task->join_waiters;
}

UBaseType_t furble_sim_task_priority(TaskHandle_t taskHandle) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  return task == nullptr ? 0 : task->priority;
}

uint64_t furble_sim_task_creation_order(TaskHandle_t taskHandle) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  return task == nullptr ? UINT64_MAX : task->creation_order;
}

bool furble_sim_task_join_claimed(TaskHandle_t taskHandle) {
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  const auto task = findTaskLocked(static_cast<SimTask *>(taskHandle));
  return task != nullptr && task->join_claimed;
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
      lock, ticksToWait, [queue]() { return queue->deleted || !queue->items.empty(); }, queue,
      SimWaitKind::queue_receive);
  exitStoppedTask();
  if (Furble::Sim::schedulerStopping() || taskStopping() || queue->deleted || !available
      || queue->items.empty()) {
    Furble::Sim::profilerQueueReceive("queue", false);
    return pdFALSE;
  }

  const auto value = std::move(queue->items.front());
  queue->items.pop_front();
  std::memcpy(item, value.data(), queue->item_size);
  releaseQueueWaiterLocked(queue, SimWaitKind::queue_send);
  dispatchNextLocked();
  preemptForHigherPriorityLocked(lock);
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
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    QueueUse use {queue};
    exitStoppedTask();
    if (queue->deleted) {
      return pdFALSE;
    }
    queue->items.clear();
    releaseQueueWaiterLocked(queue, SimWaitKind::queue_send);
    dispatchNextLocked();
    preemptForHigherPriorityLocked(lock);
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
  bool deleteNow = false;
  {
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (queue->deleted) {
      return;
    }
    queue->deleted = true;
    queue->items.clear();
    queue->reset_callback = nullptr;
    releaseAllQueueWaitersLocked(queue);
    Furble::Sim::schedulerCondition().notify_all();
    if (queue->active_users == 0) {
      deleteNow = true;
    } else {
      // A task may own the queue while another task is blocked inside a
      // receive/send. Do not wait while retaining the scheduler turn: the
      // waiter must run its QueueUse destructor to release the last user.
      if (currentTask != nullptr && runningTask == currentTask) {
        currentTask->runnable = false;
        runningTask = nullptr;
        dispatchNextLocked();
        Furble::Sim::schedulerCondition().notify_all();
      }
      Furble::Sim::schedulerCondition().wait(
          lock, [queue]() { return queue->active_users == 0 || Furble::Sim::schedulerStopping(); });
      if (queue->active_users == 0) {
        deleteNow = true;
      }
      if (currentTask != nullptr && !Furble::Sim::schedulerStopping()) {
        currentTask->runnable = true;
        currentTask->ready_order = nextReadyOrder++;
        dispatchNextLocked();
        waitForTurnLocked(lock);
      }
    }
  }
  if (deleteNow) {
    delete queue;
  }
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
