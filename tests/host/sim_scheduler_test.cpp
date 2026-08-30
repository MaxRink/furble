#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include "clock.h"

namespace Furble::Sim {

void profilerQueueReceive(const char *, bool) {}
void profilerTaskDelay(const char *, uint32_t) {}

}  // namespace Furble::Sim

namespace {

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
