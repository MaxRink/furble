#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock.h"

namespace Furble::Sim {

std::atomic<int> requestedExit {-1};

void requestExit(int result) {
  int unset = -1;
  requestedExit.compare_exchange_strong(unset, result);
}

void requestFailureExit(void) {
  int result = requestedExit.load();
  while (result == -1 || result == 0) {
    if (requestedExit.compare_exchange_weak(result, 1)) {
      return;
    }
  }
}

void profilerQueueReceive(const char *, bool) {}
void profilerTaskDelay(const char *, uint32_t) {}

}  // namespace Furble::Sim

namespace {

class TestEvent {
 public:
  void signal() {
    const std::lock_guard<std::mutex> lock(mutex_);
    signaled_ = true;
    condition_.notify_all();
  }

  void wait(void) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() { return signaled_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool signaled_ = false;
};

class TestBarrier {
 public:
  explicit TestBarrier(size_t parties) : parties_ {parties} {}

  void arriveAndWait(void) {
    std::unique_lock<std::mutex> lock(mutex_);
    ++arrived_;
    condition_.notify_all();
    condition_.wait(lock, [this]() { return open_; });
  }

  void waitForAll(void) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() { return arrived_ == parties_; });
  }

  void open(void) {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    condition_.notify_all();
  }

 private:
  const size_t parties_;
  std::mutex mutex_;
  std::condition_variable condition_;
  size_t arrived_ = 0;
  bool open_ = false;
};

int fail(int line) {
  std::cerr << "sim scheduler failure at line " << line << '\n';
  return 1;
}

void waitFor(const std::atomic<bool> &value) {
  for (unsigned int attempt = 0; attempt < 10000 && !value.load(); ++attempt) {
    std::this_thread::yield();
  }
}

void waitForBlocked(TaskHandle_t task) {
  for (unsigned int attempt = 0; attempt < 10000 && !furble_sim_task_blocked(task); ++attempt) {
    std::this_thread::yield();
  }
}

bool waitForLifecycle(TaskHandle_t task, FurbleSimTaskLifecycle expected) {
  for (unsigned int attempt = 0; attempt < 10000
       && furble_sim_task_lifecycle(task) != expected;
       ++attempt) {
    std::this_thread::yield();
  }
  const auto actual = furble_sim_task_lifecycle(task);
  if (actual != expected) {
    std::cerr << "sim scheduler lifecycle wait timed out: expected "
              << static_cast<int>(expected) << " got " << static_cast<int>(actual) << '\n';
    return false;
  }
  return true;
}

bool waitForJoinWaiters(TaskHandle_t task, size_t expected) {
  for (unsigned int attempt = 0; attempt < 10000
       && furble_sim_task_join_waiters(task) < expected;
       ++attempt) {
    std::this_thread::yield();
  }
  const size_t actual = furble_sim_task_join_waiters(task);
  if (actual < expected) {
    std::cerr << "sim scheduler join-waiter wait timed out: expected at least " << expected
              << " got " << actual << '\n';
    return false;
  }
  return true;
}

struct DelayState {
  std::atomic<bool> started {false};
  std::atomic<bool> completed {false};
  std::atomic<uint32_t> completedAt {0};
};

void delayedTask(void *argument) {
  auto &state = *static_cast<DelayState *>(argument);
  state.started.store(true);
  vTaskDelay(5);
  state.completedAt.store(xTaskGetTickCount());
  state.completed.store(true);
}

struct QueueWaitState {
  std::atomic<bool> started {false};
  std::atomic<bool> returned {false};
  std::atomic<BaseType_t> result {pdTRUE};
};

struct WaitArgument {
  QueueWaitState *state;
  QueueHandle_t queue;
};

void queueWaitTask(void *argument) {
  auto &wait = *static_cast<WaitArgument *>(argument);
  wait.state->started.store(true);
  uint32_t item = 0;
  wait.state->result.store(xQueueReceive(wait.queue, &item, portMAX_DELAY));
  wait.state->returned.store(true);
}

struct TimedWaitArgument {
  QueueWaitState *state;
  QueueHandle_t queue;
  TickType_t timeout;
};

void timedQueueWaitTask(void *argument) {
  auto &wait = *static_cast<TimedWaitArgument *>(argument);
  wait.state->started.store(true);
  uint32_t item = 0;
  wait.state->result.store(xQueueReceive(wait.queue, &item, wait.timeout));
  wait.state->returned.store(true);
}

struct BlockingDelayState {
  TestEvent entered;
};

void blockingDelayTask(void *argument) {
  auto &state = *static_cast<BlockingDelayState *>(argument);
  state.entered.signal();
  vTaskDelay(60000);
}

struct LoopWaitState {
  std::atomic<bool> started {false};
  std::atomic<uint32_t> falseReturns {0};
  QueueHandle_t queue = nullptr;
};

void loopingQueueWaitTask(void *argument) {
  auto &state = *static_cast<LoopWaitState *>(argument);
  state.started.store(true);
  for (;;) {
    uint32_t item = 0;
    if (xQueueReceive(state.queue, &item, portMAX_DELAY) == pdFALSE) {
      state.falseReturns.fetch_add(1);
    }
  }
}

struct YieldState {
  std::atomic<bool> started {false};
};

void yieldingTask(void *argument) {
  auto &state = *static_cast<YieldState *>(argument);
  state.started.store(true);
  for (;;) {
    vTaskDelay(0);
  }
}

struct DeleteOtherState {
  TestEvent entered;
  TestEvent release;
  TestEvent quiesced;
  TestEvent finish;
  std::atomic<bool> exited {false};
};

void deleteOtherTask(void *argument) {
  auto &state = *static_cast<DeleteOtherState *>(argument);
  state.entered.signal();
  state.release.wait();
  state.exited.store(true);
  state.quiesced.signal();
  state.finish.wait();
}

struct SelfDeleteState {
  std::atomic<bool> started {false};
  std::atomic<bool> returnedFromDelete {false};
};

void selfDeleteTask(void *argument) {
  auto &state = *static_cast<SelfDeleteState *>(argument);
  state.started.store(true);
  vTaskDelete(nullptr);
  state.returnedFromDelete.store(true);
}

struct StopRaceState {
  TestEvent entered;
  TestEvent release;
  TestEvent quiesced;
  TestEvent finish;
};

void stopRaceTask(void *argument) {
  auto &state = *static_cast<StopRaceState *>(argument);
  state.entered.signal();
  state.release.wait();
  state.quiesced.signal();
  state.finish.wait();
}

struct OwnedArgumentState {
  TestEvent entered;
  TestEvent release;
  TestEvent accessed;
  TestEvent finish;
  std::atomic<bool> destroyed {false};
  std::atomic<bool> accessedAfterDestroy {false};

  ~OwnedArgumentState() {
    destroyed.store(true);
  }
};

void ownedArgumentTask(void *argument) {
  auto &state = *static_cast<OwnedArgumentState *>(argument);
  state.entered.signal();
  state.release.wait();
  if (state.destroyed.load()) {
    state.accessedAfterDestroy.store(true);
  }
  state.accessed.signal();
  state.finish.wait();
}

struct ThrowingTaskState {
  TestEvent entered;
};

void throwingTask(void *argument) {
  auto &state = *static_cast<ThrowingTaskState *>(argument);
  state.entered.signal();
  throw std::runtime_error("simulated worker failure");
}

std::atomic<bool> timerFired {false};
std::atomic<bool> cancelledTimerFired {false};
std::atomic<int> timerCancellationResult {0};
std::atomic<bool> blockingTimerStarted {false};
std::atomic<bool> blockingTimerRelease {false};
std::atomic<bool> blockingTimerFinished {false};
std::atomic<bool> timerStopStarted {false};
std::atomic<bool> timerStopReturned {false};
esp_timer_handle_t cancellableTimer = nullptr;
esp_timer_handle_t cancellingTimer = nullptr;

void timerCallback(void *) {
  timerFired.store(true);
}

void cancelledTimerCallback(void *) {
  cancelledTimerFired.store(true);
}

void cancellingTimerCallback(void *) {
  const bool stopped = esp_timer_stop(cancellableTimer) == ESP_OK;
  const bool deleted = stopped && esp_timer_delete(cancellableTimer) == ESP_OK;
  if (deleted) {
    cancellableTimer = nullptr;
  }
  const bool deletedSelf = deleted && esp_timer_delete(cancellingTimer) == ESP_OK;
  if (deletedSelf) {
    cancellingTimer = nullptr;
  }
  timerCancellationResult.store(deletedSelf ? 1 : -1);
}

void blockingTimerCallback(void *) {
  blockingTimerStarted.store(true);
  while (!blockingTimerRelease.load()) {
    std::this_thread::yield();
  }
  blockingTimerFinished.store(true);
}

}  // namespace

int main() {
  using namespace Furble::Sim;
  struct Cleanup {
    ~Cleanup() { furble_sim_reset_tasks(); }
  } cleanup;
  furble_sim_reset_tasks();
  setClockMillis(0);

  QueueHandle_t queue = xQueueCreate(4, sizeof(uint32_t));
  if (queue == nullptr) {
    return fail(__LINE__);
  }
  uint32_t value = 1;
  uint32_t received = 0;
  if (xQueueSend(queue, &value, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  value = 2;
  if (xQueueSend(queue, &value, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  value = 3;
  if (xQueueSendToFront(queue, &value, 0) != pdTRUE || xQueueReceive(queue, &received, 0) != pdTRUE
      || received != 3 || xQueueReceive(queue, &received, 0) != pdTRUE || received != 1) {
    return fail(__LINE__);
  }
  if (xQueueReset(queue) != pdTRUE || xQueueReceive(queue, &received, 0) != pdFALSE) {
    return fail(__LINE__);
  }

  DelayState delay;
  TaskHandle_t delayTask = nullptr;
  if (xTaskCreate(delayedTask, "worker", 0, &delay, 0, &delayTask) != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(delay.started);
  if (!delay.started.load()) {
    return fail(__LINE__);
  }
  waitForBlocked(delayTask);
  if (!furble_sim_task_blocked(delayTask)) {
    return fail(__LINE__);
  }
  advanceClock(4);
  if (delay.completed.load()) {
    return fail(__LINE__);
  }
  advanceClock(1);
  waitFor(delay.completed);
  if (!delay.completed.load() || delay.completedAt.load() != 5) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  setClockMillis(UINT32_MAX - 2U);
  DelayState wrapped;
  TaskHandle_t wrappedTask = nullptr;
  if (xTaskCreate(delayedTask, "wrapped", 0, &wrapped, 0, &wrappedTask) != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(wrapped.started);
  waitForBlocked(wrappedTask);
  if (!furble_sim_task_blocked(wrappedTask)) {
    return fail(__LINE__);
  }
  advanceClock(4);
  if (wrapped.completed.load()) {
    return fail(__LINE__);
  }
  advanceClock(1);
  waitFor(wrapped.completed);
  if (!wrapped.completed.load() || wrapped.completedAt.load() != 2) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  setClockMillis(0);
  timerFired.store(false);
  const esp_timer_create_args_t timerArgs = {timerCallback, nullptr, ESP_TIMER_TASK,
                                             "scheduler-test", false};
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&timerArgs, &timer) != ESP_OK
      || esp_timer_start_once(timer, 5000) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClock(4);
  if (timerFired.load()) {
    return fail(__LINE__);
  }
  advanceClock(1);
  waitFor(timerFired);
  if (!timerFired.load()) {
    return fail(__LINE__);
  }
  esp_timer_delete(timer);

  esp_timer_handle_t stateTimer = nullptr;
  if (esp_timer_create(&timerArgs, &stateTimer) != ESP_OK
      || esp_timer_start_once(stateTimer, 10000) != ESP_OK
      || esp_timer_start_once(stateTimer, 10000) != ESP_ERR_INVALID_STATE
      || esp_timer_delete(stateTimer) != ESP_ERR_INVALID_STATE
      || esp_timer_stop(stateTimer) != ESP_OK || esp_timer_stop(stateTimer) != ESP_ERR_INVALID_STATE
      || esp_timer_delete(stateTimer) != ESP_OK) {
    return fail(__LINE__);
  }

  cancelledTimerFired.store(false);
  timerCancellationResult.store(0);
  const esp_timer_create_args_t cancellingArgs = {cancellingTimerCallback, nullptr, ESP_TIMER_TASK,
                                                  "timer-canceller", false};
  const esp_timer_create_args_t cancelledArgs = {cancelledTimerCallback, nullptr, ESP_TIMER_TASK,
                                                 "timer-cancelled", false};
  if (esp_timer_create(&cancellingArgs, &cancellingTimer) != ESP_OK
      || esp_timer_create(&cancelledArgs, &cancellableTimer) != ESP_OK
      || esp_timer_start_once(cancellingTimer, 5000) != ESP_OK
      || esp_timer_start_once(cancellableTimer, 5000) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClock(5);
  for (unsigned int attempt = 0; attempt < 10000 && timerCancellationResult.load() == 0;
       ++attempt) {
    std::this_thread::yield();
  }
  if (timerCancellationResult.load() != 1 || cancelledTimerFired.load()) {
    return fail(__LINE__);
  }

  // Owner teardown may free a timer callback argument only after the single
  // dispatcher has joined. Prove stop waits for an in-flight callback instead
  // of returning while that callback can still access owner state.
  const esp_timer_create_args_t blockingArgs = {blockingTimerCallback, nullptr, ESP_TIMER_TASK,
                                                "timer-quiescence", false};
  esp_timer_handle_t blockingTimer = nullptr;
  blockingTimerStarted.store(false);
  blockingTimerRelease.store(false);
  blockingTimerFinished.store(false);
  timerStopStarted.store(false);
  timerStopReturned.store(false);
  if (esp_timer_create(&blockingArgs, &blockingTimer) != ESP_OK
      || esp_timer_start_once(blockingTimer, 1000) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClock(1);
  waitFor(blockingTimerStarted);
  if (!blockingTimerStarted.load()) {
    return fail(__LINE__);
  }
  std::thread timerStopper([]() {
    timerStopStarted.store(true);
    furble_sim_stop_all_timers();
    timerStopReturned.store(true);
  });
  waitFor(timerStopStarted);
  for (unsigned int attempt = 0; attempt < 10000 && !timerStopReturned.load(); ++attempt) {
    std::this_thread::yield();
  }
  const bool returnedBeforeCallback = timerStopReturned.load();
  blockingTimerRelease.store(true);
  timerStopper.join();
  if (returnedBeforeCallback || !blockingTimerFinished.load() || !timerStopReturned.load()
      || esp_timer_delete(blockingTimer) != ESP_OK) {
    return fail(__LINE__);
  }

  esp_timer_handle_t stoppedTimer = nullptr;
  if (esp_timer_create(&timerArgs, &stoppedTimer) != ESP_OK) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  if (esp_timer_start_once(stoppedTimer, 1000) != ESP_FAIL) {
    return fail(__LINE__);
  }
  furble_sim_reset_tasks();
  timerFired.store(false);
  if (esp_timer_start_once(stoppedTimer, 1000) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClock(1);
  waitFor(timerFired);
  if (!timerFired.load()) {
    return fail(__LINE__);
  }
  esp_timer_delete(stoppedTimer);

  QueueWaitState deleted;
  QueueHandle_t deletedQueue = xQueueCreate(1, sizeof(uint32_t));
  WaitArgument deletedArgument {&deleted, deletedQueue};
  TaskHandle_t deletedTask = nullptr;
  if (deletedQueue == nullptr
      || xTaskCreate(queueWaitTask, "queue-delete", 0, &deletedArgument, 0, &deletedTask)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(deleted.started);
  if (!deleted.started.load()) {
    return fail(__LINE__);
  }
  waitForBlocked(deletedTask);
  if (!furble_sim_task_blocked(deletedTask)) {
    return fail(__LINE__);
  }
  vQueueDelete(deletedQueue);
  waitFor(deleted.returned);
  if (!deleted.returned.load() || deleted.result.load() != pdFALSE) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  requestedExit.store(-1);
  requestExit(0);
  ThrowingTaskState throwing;
  TaskHandle_t throwingTaskHandle = nullptr;
  if (xTaskCreate(throwingTask, "throwing", 0, &throwing, 0, &throwingTaskHandle) != pdPASS) {
    return fail(__LINE__);
  }
  throwing.entered.wait();
  vTaskDelete(throwingTaskHandle);
  if (requestedExit.load() != 1 || !furble_sim_shutdown_requested()) {
    return fail(__LINE__);
  }
  furble_sim_reset_tasks();

  // An external delete wakes a task blocked in a finite virtual delay and
  // waits for its worker to finish before returning.
  BlockingDelayState blockingDelay;
  TaskHandle_t blockingDelayTaskHandle = nullptr;
  if (xTaskCreate(blockingDelayTask, "delete-delay", 0, &blockingDelay, 0,
                  &blockingDelayTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  blockingDelay.entered.wait();
  waitForBlocked(blockingDelayTaskHandle);
  if (!furble_sim_task_blocked(blockingDelayTaskHandle)) {
    return fail(__LINE__);
  }
  TestEvent delayDeleteEntered;
  std::atomic<bool> delayDeleteReturned {false};
  std::thread delayDeleter([&]() {
    delayDeleteEntered.signal();
    vTaskDelete(blockingDelayTaskHandle);
    delayDeleteReturned.store(true);
  });
  delayDeleteEntered.wait();
  delayDeleter.join();
  if (!delayDeleteReturned.load() || furble_sim_task_blocked(blockingDelayTaskHandle)
      || furble_sim_task_lifecycle(blockingDelayTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }

  // The same quiescence contract applies to a finite queue receive.
  QueueWaitState finiteQueueWait;
  QueueHandle_t finiteQueue = xQueueCreate(1, sizeof(uint32_t));
  TimedWaitArgument finiteQueueArgument {&finiteQueueWait, finiteQueue, 60000};
  TaskHandle_t finiteQueueTaskHandle = nullptr;
  if (finiteQueue == nullptr
      || xTaskCreate(timedQueueWaitTask, "delete-finite-queue", 0, &finiteQueueArgument, 0,
                     &finiteQueueTaskHandle)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(finiteQueueWait.started);
  waitForBlocked(finiteQueueTaskHandle);
  if (!furble_sim_task_blocked(finiteQueueTaskHandle)) {
    return fail(__LINE__);
  }
  TestEvent finiteDeleteEntered;
  std::atomic<bool> finiteDeleteReturned {false};
  std::thread finiteDeleter([&]() {
    finiteDeleteEntered.signal();
    vTaskDelete(finiteQueueTaskHandle);
    finiteDeleteReturned.store(true);
  });
  finiteDeleteEntered.wait();
  finiteDeleter.join();
  if (!finiteDeleteReturned.load() || finiteQueueWait.returned.load()
      || furble_sim_task_lifecycle(finiteQueueTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }
  vQueueDelete(finiteQueue);

  // A portMAX_DELAY queue receive must also be interrupted by external delete.
  QueueWaitState maxQueueWait;
  QueueHandle_t maxQueue = xQueueCreate(1, sizeof(uint32_t));
  WaitArgument maxQueueArgument {&maxQueueWait, maxQueue};
  TaskHandle_t maxQueueTaskHandle = nullptr;
  if (maxQueue == nullptr
      || xTaskCreate(queueWaitTask, "delete-max-queue", 0, &maxQueueArgument, 0,
                     &maxQueueTaskHandle)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(maxQueueWait.started);
  waitForBlocked(maxQueueTaskHandle);
  if (!furble_sim_task_blocked(maxQueueTaskHandle)) {
    return fail(__LINE__);
  }
  TestEvent maxDeleteEntered;
  std::atomic<bool> maxDeleteReturned {false};
  std::thread maxDeleter([&]() {
    maxDeleteEntered.signal();
    vTaskDelete(maxQueueTaskHandle);
    maxDeleteReturned.store(true);
  });
  maxDeleteEntered.wait();
  maxDeleter.join();
  if (!maxDeleteReturned.load() || maxQueueWait.returned.load()
      || furble_sim_task_lifecycle(maxQueueTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }
  vQueueDelete(maxQueue);

  // A naturally finished task remains joinable until an external delete claims
  // its join.
  TestEvent naturallyFinished;
  struct NaturalTaskState {
    TestEvent *finished;
  } naturalState {&naturallyFinished};
  auto naturalTask = [](void *argument) {
    auto &state = *static_cast<NaturalTaskState *>(argument);
    state.finished->signal();
  };
  TaskHandle_t naturalTaskHandle = nullptr;
  if (xTaskCreate(naturalTask, "natural-finish", 0, &naturalState, 0, &naturalTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  naturallyFinished.wait();
  if (!waitForLifecycle(naturalTaskHandle, FURBLE_SIM_TASK_FINISHED)) {
    return fail(__LINE__);
  }
  vTaskDelete(naturalTaskHandle);
  if (furble_sim_task_lifecycle(naturalTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }

  // Deleting another task requests cooperative unwind, then waits until the
  // target is quiescent before claiming its join. The entry event and held
  // target gate make the wait observable without a timing assumption.
  DeleteOtherState deleteOther;
  TaskHandle_t deleteOtherTaskHandle = nullptr;
  if (xTaskCreate(deleteOtherTask, "delete-other", 0, &deleteOther, 0,
                  &deleteOtherTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  deleteOther.entered.wait();
  TestEvent deleteOtherEntered;
  std::atomic<bool> deleteOtherReturned {false};
  std::thread deleteOtherCaller([&]() {
    deleteOtherEntered.signal();
    vTaskDelete(deleteOtherTaskHandle);
    deleteOtherReturned.store(true);
  });
  deleteOtherEntered.wait();
  const bool deleteOtherStopRequested =
      waitForLifecycle(deleteOtherTaskHandle, FURBLE_SIM_TASK_STOP_REQUESTED);
  const bool deleteOtherJoinWaiter = waitForJoinWaiters(deleteOtherTaskHandle, 1);
  if (!deleteOtherStopRequested || !deleteOtherJoinWaiter || deleteOtherReturned.load()
      || furble_sim_task_join_claimed(deleteOtherTaskHandle)) {
    deleteOther.release.signal();
    deleteOther.finish.signal();
    deleteOtherCaller.join();
    return fail(__LINE__);
  }
  deleteOther.release.signal();
  deleteOther.quiesced.wait();
  if (deleteOtherReturned.load()
      || furble_sim_task_lifecycle(deleteOtherTaskHandle) != FURBLE_SIM_TASK_STOP_REQUESTED) {
    deleteOther.finish.signal();
    deleteOtherCaller.join();
    return fail(__LINE__);
  }
  deleteOther.finish.signal();
  deleteOtherCaller.join();
  if (!deleteOther.exited.load() || !deleteOtherReturned.load()
      || !furble_sim_task_join_claimed(deleteOtherTaskHandle)
      || furble_sim_task_lifecycle(deleteOtherTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }
  furble_sim_reset_tasks();
  vTaskDelete(deleteOtherTaskHandle);
  if (!deleteOther.exited.load() || !deleteOtherReturned.load()) {
    return fail(__LINE__);
  }

  // The caller-owned task argument is destroyed only after vTaskDelete has
  // returned. The target performs its final access before a second gate lets
  // it return, so a later access cannot race argument destruction.
  auto ownedArgument = std::make_unique<OwnedArgumentState>();
  TaskHandle_t ownedArgumentTaskHandle = nullptr;
  if (xTaskCreate(ownedArgumentTask, "owned-argument", 0, ownedArgument.get(), 0,
                  &ownedArgumentTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  ownedArgument->entered.wait();
  TestEvent ownedDeleteEntered;
  std::atomic<bool> ownedDeleteReturned {false};
  std::thread ownedDeleter([&]() {
    ownedDeleteEntered.signal();
    vTaskDelete(ownedArgumentTaskHandle);
    ownedDeleteReturned.store(true);
  });
  ownedDeleteEntered.wait();
  const bool ownedStopRequested =
      waitForLifecycle(ownedArgumentTaskHandle, FURBLE_SIM_TASK_STOP_REQUESTED);
  const bool ownedJoinWaiter = waitForJoinWaiters(ownedArgumentTaskHandle, 1);
  if (!ownedStopRequested || !ownedJoinWaiter || ownedDeleteReturned.load()
      || furble_sim_task_join_claimed(ownedArgumentTaskHandle)) {
    ownedArgument->release.signal();
    ownedArgument->finish.signal();
    ownedDeleter.join();
    return fail(__LINE__);
  }
  ownedArgument->release.signal();
  ownedArgument->accessed.wait();
  if (ownedDeleteReturned.load()) {
    ownedArgument->finish.signal();
    ownedDeleter.join();
    return fail(__LINE__);
  }
  const bool ownedStillStopRequested =
      waitForLifecycle(ownedArgumentTaskHandle, FURBLE_SIM_TASK_STOP_REQUESTED);
  const bool ownedStillHasJoinWaiter = waitForJoinWaiters(ownedArgumentTaskHandle, 1);
  if (!ownedStillStopRequested || !ownedStillHasJoinWaiter
      || furble_sim_task_join_claimed(ownedArgumentTaskHandle)) {
    ownedArgument->finish.signal();
    ownedDeleter.join();
    return fail(__LINE__);
  }
  ownedArgument->finish.signal();
  ownedDeleter.join();
  const bool accessedAfterDestroy = ownedArgument->accessedAfterDestroy.load();
  if (!ownedDeleteReturned.load() || accessedAfterDestroy
      || !furble_sim_task_join_claimed(ownedArgumentTaskHandle)
      || furble_sim_task_lifecycle(ownedArgumentTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }
  ownedArgument.reset();

  // A task deleting itself unwinds at the task boundary. It never attempts to
  // join its own std::thread.
  SelfDeleteState selfDelete;
  TaskHandle_t selfDeleteTaskHandle = nullptr;
  if (xTaskCreate(selfDeleteTask, "self-delete", 0, &selfDelete, 0, &selfDeleteTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(selfDelete.started);
  if (!selfDelete.started.load()) {
    return fail(__LINE__);
  }
  vTaskDelete(selfDeleteTaskHandle);
  if (selfDelete.returnedFromDelete.load()
      || furble_sim_task_lifecycle(selfDeleteTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }
  furble_sim_reset_tasks();
  vTaskDelete(selfDeleteTaskHandle);
  if (furble_sim_task_lifecycle(selfDeleteTaskHandle) != FURBLE_SIM_TASK_JOINED) {
    return fail(__LINE__);
  }

  // Repeat a barrier-controlled race between delete-other and global stop.
  // Both callers are held at the target gate until each has entered its wait.
  for (unsigned int iteration = 0; iteration < 8; ++iteration) {
    furble_sim_reset_tasks();
    StopRaceState stopRace;
    TaskHandle_t stopRaceTaskHandle = nullptr;
    if (xTaskCreate(stopRaceTask, "stop-race", 0, &stopRace, 0, &stopRaceTaskHandle)
        != pdPASS) {
      return fail(__LINE__);
    }
    stopRace.entered.wait();
    TestBarrier callers {2};
    TestEvent deleteEntered;
    TestEvent stopEntered;
    std::atomic<bool> deleteReturned {false};
    std::atomic<bool> stopReturned {false};
    std::thread stopRaceDelete([&]() {
      callers.arriveAndWait();
      deleteEntered.signal();
      vTaskDelete(stopRaceTaskHandle);
      deleteReturned.store(true);
    });
    std::thread stopRaceStopper([&]() {
      callers.arriveAndWait();
      stopEntered.signal();
      furble_sim_stop_all_tasks();
      stopReturned.store(true);
    });
    callers.waitForAll();
    callers.open();
    deleteEntered.wait();
    stopEntered.wait();
    const bool stopRaceStopRequested =
        waitForLifecycle(stopRaceTaskHandle, FURBLE_SIM_TASK_STOP_REQUESTED);
    const bool stopRaceJoinWaiters = waitForJoinWaiters(stopRaceTaskHandle, 2);
    if (!stopRaceStopRequested || !stopRaceJoinWaiters || deleteReturned.load()
        || stopReturned.load()
        || furble_sim_task_join_claimed(stopRaceTaskHandle)) {
      stopRace.release.signal();
      stopRace.finish.signal();
      stopRaceDelete.join();
      stopRaceStopper.join();
      return fail(__LINE__);
    }
    stopRace.release.signal();
    stopRace.quiesced.wait();
    if (deleteReturned.load() || stopReturned.load()
        || furble_sim_task_lifecycle(stopRaceTaskHandle) != FURBLE_SIM_TASK_STOP_REQUESTED) {
      stopRace.finish.signal();
      stopRaceDelete.join();
      stopRaceStopper.join();
      return fail(__LINE__);
    }
    stopRace.finish.signal();
    stopRaceDelete.join();
    stopRaceStopper.join();
    if (!deleteReturned.load() || !stopReturned.load()
        || !furble_sim_task_join_claimed(stopRaceTaskHandle)
        || furble_sim_task_lifecycle(stopRaceTaskHandle) != FURBLE_SIM_TASK_JOINED) {
      return fail(__LINE__);
    }
  }
  furble_sim_reset_tasks();

  QueueWaitState shutdown;
  QueueHandle_t shutdownQueue = xQueueCreate(1, sizeof(uint32_t));
  if (shutdownQueue == nullptr) {
    return fail(__LINE__);
  }
  // Use a task-local queue argument while retaining the state for assertions.
  WaitArgument waitArgument {&shutdown, shutdownQueue};
  TaskHandle_t shutdownTask = nullptr;
  if (xTaskCreate(queueWaitTask, "queue-wait", 0, &waitArgument, 0, &shutdownTask) != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(shutdown.started);
  if (!shutdown.started.load()) {
    return 1;
  }
  waitForBlocked(shutdownTask);
  if (!furble_sim_task_blocked(shutdownTask)) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  if (shutdown.returned.load() || !furble_sim_shutdown_requested()) {
    return 1;
  }

  vQueueDelete(shutdownQueue);

  furble_sim_reset_tasks();
  LoopWaitState looping;
  looping.queue = xQueueCreate(1, sizeof(uint32_t));
  TaskHandle_t loopingTask = nullptr;
  if (looping.queue == nullptr
      || xTaskCreate(loopingQueueWaitTask, "queue-loop", 0, &looping, 0, &loopingTask) != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(looping.started);
  waitForBlocked(loopingTask);
  if (!looping.started.load() || !furble_sim_task_blocked(loopingTask)) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  if (looping.falseReturns.load() != 0) {
    return fail(__LINE__);
  }
  vQueueDelete(looping.queue);

  furble_sim_reset_tasks();
  YieldState yielding;
  if (xTaskCreate(yieldingTask, "yield-loop", 0, &yielding, 0, nullptr) != pdPASS) {
    return fail(__LINE__);
  }
  waitFor(yielding.started);
  if (!yielding.started.load()) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();

  vQueueDelete(queue);
  furble_sim_reset_tasks();
  return 0;
}
