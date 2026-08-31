#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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

// These match the production task priorities in src/main.cpp and the
// connection task declarations. Keep scheduler tests tied to real firmware
// values instead of synthetic priority bands.
constexpr UBaseType_t kControlPriority = 4;
constexpr UBaseType_t kGpsPriority = 3;
constexpr UBaseType_t kCompanionPriority = 2;

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
  for (unsigned int attempt = 0; attempt < 10000 && furble_sim_task_lifecycle(task) != expected;
       ++attempt) {
    std::this_thread::yield();
  }
  const auto actual = furble_sim_task_lifecycle(task);
  if (actual != expected) {
    std::cerr << "sim scheduler lifecycle wait timed out: expected " << static_cast<int>(expected)
              << " got " << static_cast<int>(actual) << '\n';
    return false;
  }
  return true;
}

bool waitForJoinWaiters(TaskHandle_t task, size_t expected) {
  for (unsigned int attempt = 0; attempt < 10000 && furble_sim_task_join_waiters(task) < expected;
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

  ~OwnedArgumentState() { destroyed.store(true); }
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

struct PriorityTaskState {
  TestEvent finished;
  std::mutex *orderMutex;
  std::vector<int> *order;
  int marker;
  int turns;

  PriorityTaskState(std::mutex *mutex, std::vector<int> *values, int markerValue, int turnCount)
      : orderMutex {mutex}, order {values}, marker {markerValue}, turns {turnCount} {}
};

void priorityTask(void *argument) {
  auto &state = *static_cast<PriorityTaskState *>(argument);
  vTaskDelay(5);
  for (int turn = 0; turn < state.turns; ++turn) {
    {
      const std::lock_guard<std::mutex> lock(*state.orderMutex);
      state.order->push_back(state.marker + turn);
    }
    vTaskDelay(0);
  }
  state.finished.signal();
}

struct OrderedQueueWaitState {
  TestEvent finished;
  QueueHandle_t queue;
  std::mutex *orderMutex;
  std::vector<int> *order;
  int marker;
  std::atomic<BaseType_t> result {pdFALSE};

  OrderedQueueWaitState(QueueHandle_t queueHandle,
                        std::mutex *mutex,
                        std::vector<int> *values,
                        int markerValue)
      : queue {queueHandle}, orderMutex {mutex}, order {values}, marker {markerValue} {}
};

void orderedQueueWaitTask(void *argument) {
  auto &state = *static_cast<OrderedQueueWaitState *>(argument);
  uint32_t value = 0;
  state.result.store(xQueueReceive(state.queue, &value, portMAX_DELAY));
  {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(state.marker);
  }
  state.finished.signal();
}

struct QueueSendPreemptState {
  TestEvent finished;
  QueueHandle_t queue;
  std::mutex *orderMutex;
  std::vector<int> *order;

  QueueSendPreemptState(QueueHandle_t queueHandle, std::mutex *mutex, std::vector<int> *values)
      : queue {queueHandle}, orderMutex {mutex}, order {values} {}
};

void queueSendPreemptTask(void *argument) {
  auto &state = *static_cast<QueueSendPreemptState *>(argument);
  const uint32_t value = 1;
  if (xQueueSend(state.queue, &value, 0) == pdTRUE) {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(2);
  }
  state.finished.signal();
}

struct TimedQueueWaitState {
  TestEvent finished;
  QueueHandle_t queue;
  std::atomic<BaseType_t> result {pdFALSE};
  std::atomic<uint32_t> value {0};

  explicit TimedQueueWaitState(QueueHandle_t queueHandle) : queue {queueHandle} {}
};

void timedQueueWaitOnceTask(void *argument) {
  auto &state = *static_cast<TimedQueueWaitState *>(argument);
  uint32_t value = 0;
  state.result.store(xQueueReceive(state.queue, &value, 5));
  state.value.store(value);
  state.finished.signal();
}

struct QueueOwnerDeleteState {
  TestEvent finished;
  QueueHandle_t queue;

  explicit QueueOwnerDeleteState(QueueHandle_t queueHandle) : queue {queueHandle} {}
};

void queueOwnerDeleteTask(void *argument) {
  auto &state = *static_cast<QueueOwnerDeleteState *>(argument);
  vQueueDelete(state.queue);
  state.finished.signal();
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

std::atomic<bool> subMillisecondTimerFired {false};
std::mutex timerOrderMutex;
std::vector<int> timerOrder;

void subMillisecondTimerCallback(void *) {
  {
    const std::lock_guard<std::mutex> lock(timerOrderMutex);
    timerOrder.push_back(1);
  }
  subMillisecondTimerFired.store(true);
}

struct TimerTaskState {
  TestEvent finished;
};

void timerOrderTask(void *argument) {
  auto &state = *static_cast<TimerTaskState *>(argument);
  vTaskDelay(5);
  {
    const std::lock_guard<std::mutex> lock(timerOrderMutex);
    timerOrder.push_back(2);
  }
  state.finished.signal();
}

struct CreatePreemptState {
  TestEvent childFinished;
  TestEvent finished;
  std::mutex *orderMutex;
  std::vector<int> *order;
  TaskHandle_t *childHandleStorage = nullptr;
  std::atomic<bool> childSawPublishedHandle {false};

  CreatePreemptState(std::mutex *mutex, std::vector<int> *values)
      : orderMutex {mutex}, order {values} {}
};

void createPreemptChildTask(void *argument) {
  auto &state = *static_cast<CreatePreemptState *>(argument);
  // The ESP-IDF contract publishes the handle before a higher-priority child
  // can run. Check that contract from inside the child, before its parent can
  // observe xTaskCreate returning.
  state.childSawPublishedHandle.store(state.childHandleStorage != nullptr
                                      && *state.childHandleStorage != nullptr);
  {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(1);
  }
  state.childFinished.signal();
}

void createPreemptParentTask(void *argument) {
  auto &state = *static_cast<CreatePreemptState *>(argument);
  TaskHandle_t child = nullptr;
  state.childHandleStorage = &child;
  if (xTaskCreate(createPreemptChildTask, "created-high", 0, &state, kControlPriority, &child)
      != pdPASS) {
    state.finished.signal();
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(2);
  }
  state.finished.signal();
}

struct QueueResetPreemptSenderState {
  TestEvent finished;
  QueueHandle_t queue;
  std::mutex *orderMutex;
  std::vector<int> *order;

  QueueResetPreemptSenderState(QueueHandle_t queueHandle,
                               std::mutex *mutex,
                               std::vector<int> *values)
      : queue {queueHandle}, orderMutex {mutex}, order {values} {}
};

void queueResetPreemptSenderTask(void *argument) {
  auto &state = *static_cast<QueueResetPreemptSenderState *>(argument);
  const uint32_t value = 2;
  if (xQueueSend(state.queue, &value, portMAX_DELAY) == pdTRUE) {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(1);
  }
  state.finished.signal();
}

struct QueueResetPreemptOwnerState {
  TestEvent finished;
  QueueHandle_t queue;
  std::mutex *orderMutex;
  std::vector<int> *order;

  QueueResetPreemptOwnerState(QueueHandle_t queueHandle,
                              std::mutex *mutex,
                              std::vector<int> *values)
      : queue {queueHandle}, orderMutex {mutex}, order {values} {}
};

void queueResetPreemptOwnerTask(void *argument) {
  auto &state = *static_cast<QueueResetPreemptOwnerState *>(argument);
  if (xQueueReset(state.queue) == pdTRUE) {
    const std::lock_guard<std::mutex> lock(*state.orderMutex);
    state.order->push_back(2);
  }
  state.finished.signal();
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

  // Runnable tasks at one virtual deadline are selected by FreeRTOS priority.
  // A higher-priority task may continue to run through zero-tick yields, while
  // equal-priority tasks rotate in deterministic creation order.
  setClockMillis(0);
  std::vector<int> priorityOrder;
  std::mutex priorityOrderMutex;
  PriorityTaskState highPriority {&priorityOrderMutex, &priorityOrder, 30, 3};
  PriorityTaskState lowPriority {&priorityOrderMutex, &priorityOrder, 10, 3};
  TaskHandle_t highPriorityTask = nullptr;
  TaskHandle_t lowPriorityTask = nullptr;
  if (xTaskCreate(priorityTask, "priority-high", 0, &highPriority, kControlPriority,
                  &highPriorityTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(highPriorityTask);
  if (xTaskCreate(priorityTask, "priority-low", 0, &lowPriority, kCompanionPriority,
                  &lowPriorityTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(lowPriorityTask);
  if (furble_sim_task_priority(highPriorityTask) != kControlPriority
      || furble_sim_task_priority(lowPriorityTask) != kCompanionPriority
      || furble_sim_task_creation_order(highPriorityTask)
             >= furble_sim_task_creation_order(lowPriorityTask)) {
    return fail(__LINE__);
  }
  advanceClock(5);
  highPriority.finished.wait();
  lowPriority.finished.wait();
  const std::vector<int> expectedPriorityOrder {30, 31, 32, 10, 11, 12};
  if (priorityOrder != expectedPriorityOrder) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  // A queue wake selects the highest-priority waiter, then the oldest waiter
  // among equal priorities. A single item must leave every other waiter
  // blocked until another item arrives.
  setClockMillis(0);
  std::vector<int> queueOrder;
  std::mutex queueOrderMutex;
  QueueHandle_t orderedQueue = xQueueCreate(3, sizeof(uint32_t));
  OrderedQueueWaitState orderedLow {orderedQueue, &queueOrderMutex, &queueOrder, 10};
  OrderedQueueWaitState orderedHighFirst {orderedQueue, &queueOrderMutex, &queueOrder, 20};
  OrderedQueueWaitState orderedHighSecond {orderedQueue, &queueOrderMutex, &queueOrder, 30};
  TaskHandle_t orderedLowTask = nullptr;
  TaskHandle_t orderedHighFirstTask = nullptr;
  TaskHandle_t orderedHighSecondTask = nullptr;
  if (orderedQueue == nullptr
      || xTaskCreate(orderedQueueWaitTask, "ordered-low", 0, &orderedLow, kCompanionPriority,
                     &orderedLowTask)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(orderedLowTask);
  if (xTaskCreate(orderedQueueWaitTask, "ordered-high-first", 0, &orderedHighFirst,
                  kControlPriority, &orderedHighFirstTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(orderedHighFirstTask);
  if (xTaskCreate(orderedQueueWaitTask, "ordered-high-second", 0, &orderedHighSecond,
                  kControlPriority, &orderedHighSecondTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(orderedHighSecondTask);
  uint32_t queueValue = 1;
  if (xQueueSend(orderedQueue, &queueValue, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  orderedHighFirst.finished.wait();
  if (orderedLow.result.load() != pdFALSE || orderedHighSecond.result.load() != pdFALSE
      || !furble_sim_task_blocked(orderedLowTask)
      || !furble_sim_task_blocked(orderedHighSecondTask)) {
    return fail(__LINE__);
  }
  queueValue = 2;
  if (xQueueSend(orderedQueue, &queueValue, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  orderedHighSecond.finished.wait();
  queueValue = 3;
  if (xQueueSend(orderedQueue, &queueValue, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  orderedLow.finished.wait();
  if (queueOrder != std::vector<int> {20, 30, 10}) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();
  vQueueDelete(orderedQueue);

  // A send from a lower-priority task must yield at the queue boundary when it
  // wakes a higher-priority receiver. The sender resumes only after receipt.
  setClockMillis(0);
  queueOrder.clear();
  QueueHandle_t preemptQueue = xQueueCreate(1, sizeof(uint32_t));
  OrderedQueueWaitState preemptReceiver {preemptQueue, &queueOrderMutex, &queueOrder, 1};
  QueueSendPreemptState preemptSender {preemptQueue, &queueOrderMutex, &queueOrder};
  TaskHandle_t preemptReceiverTask = nullptr;
  TaskHandle_t preemptSenderTask = nullptr;
  if (preemptQueue == nullptr
      || xTaskCreate(orderedQueueWaitTask, "preempt-receiver", 0, &preemptReceiver,
                     kControlPriority, &preemptReceiverTask)
             != pdPASS
      || xTaskCreate(queueSendPreemptTask, "preempt-sender", 0, &preemptSender, kCompanionPriority,
                     &preemptSenderTask)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(preemptReceiverTask);
  preemptSender.finished.wait();
  preemptReceiver.finished.wait();
  if (queueOrder != std::vector<int> {1, 2}) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();
  vQueueDelete(preemptQueue);

  // Creating a higher-priority task from a modeled lower-priority task is a
  // scheduler boundary. The child must run before xTaskCreate returns.
  setClockMillis(0);
  queueOrder.clear();
  CreatePreemptState createPreempt {&queueOrderMutex, &queueOrder};
  TaskHandle_t createParentTaskHandle = nullptr;
  if (xTaskCreate(createPreemptParentTask, "create-parent", 0, &createPreempt, kCompanionPriority,
                  &createParentTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  createPreempt.finished.wait();
  if (queueOrder != std::vector<int> {1, 2} || !createPreempt.childSawPublishedHandle.load()) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  // Resetting a full queue wakes a blocked higher-priority sender. The sender
  // must preempt the lower-priority task at the reset boundary.
  setClockMillis(0);
  queueOrder.clear();
  QueueHandle_t resetQueue = xQueueCreate(1, sizeof(uint32_t));
  const uint32_t queuedValue = 1;
  if (resetQueue == nullptr || xQueueSend(resetQueue, &queuedValue, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  QueueResetPreemptSenderState resetSender {resetQueue, &queueOrderMutex, &queueOrder};
  QueueResetPreemptOwnerState resetOwner {resetQueue, &queueOrderMutex, &queueOrder};
  TaskHandle_t resetSenderTask = nullptr;
  TaskHandle_t resetOwnerTask = nullptr;
  if (xTaskCreate(queueResetPreemptSenderTask, "reset-sender", 0, &resetSender, kControlPriority,
                  &resetSenderTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(resetSenderTask);
  if (xTaskCreate(queueResetPreemptOwnerTask, "reset-owner", 0, &resetOwner, kCompanionPriority,
                  &resetOwnerTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  resetOwner.finished.wait();
  resetSender.finished.wait();
  if (queueOrder != std::vector<int> {1, 2}) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();
  vQueueDelete(resetQueue);

  // A timed receive latches a timeout at the exact deadline. An item sent
  // after that boundary must remain queued and cannot retroactively succeed.
  setClockMillis(0);
  QueueHandle_t deadlineQueue = xQueueCreate(1, sizeof(uint32_t));
  TimedQueueWaitState deadlineWait {deadlineQueue};
  TaskHandle_t deadlineTask = nullptr;
  if (deadlineQueue == nullptr
      || xTaskCreate(timedQueueWaitOnceTask, "deadline-receive", 0, &deadlineWait,
                     kCompanionPriority, &deadlineTask)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(deadlineTask);
  advanceClock(5);
  deadlineWait.finished.wait();
  if (deadlineWait.result.load() != pdFALSE) {
    return fail(__LINE__);
  }
  queueValue = 9;
  if (xQueueSend(deadlineQueue, &queueValue, 0) != pdTRUE) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();
  vQueueDelete(deadlineQueue);

  // Queue deletion from a task owner wakes an active blocked user and waits
  // without holding the scheduler turn, so the final QueueUse can drain.
  setClockMillis(0);
  QueueHandle_t ownedQueue = xQueueCreate(1, sizeof(uint32_t));
  QueueWaitState ownedWait;
  WaitArgument ownedWaitArgument {&ownedWait, ownedQueue};
  QueueOwnerDeleteState ownerDelete {ownedQueue};
  TaskHandle_t ownedWaitTask = nullptr;
  TaskHandle_t ownerDeleteTaskHandle = nullptr;
  if (ownedQueue == nullptr
      || xTaskCreate(queueWaitTask, "owned-queue-wait", 0, &ownedWaitArgument, kCompanionPriority,
                     &ownedWaitTask)
             != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(ownedWaitTask);
  if (xTaskCreate(queueOwnerDeleteTask, "owned-queue-delete", 0, &ownerDelete, kControlPriority,
                  &ownerDeleteTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  ownerDelete.finished.wait();
  if (!ownedWait.returned.load() || ownedWait.result.load() != pdFALSE) {
    return fail(__LINE__);
  }
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

  setClockMillis(0);
  priorityOrder.clear();
  PriorityTaskState firstEqual {&priorityOrderMutex, &priorityOrder, 100, 3};
  PriorityTaskState secondEqual {&priorityOrderMutex, &priorityOrder, 200, 3};
  TaskHandle_t firstEqualTask = nullptr;
  TaskHandle_t secondEqualTask = nullptr;
  if (xTaskCreate(priorityTask, "equal-first", 0, &firstEqual, kGpsPriority, &firstEqualTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(firstEqualTask);
  if (xTaskCreate(priorityTask, "equal-second", 0, &secondEqual, kGpsPriority, &secondEqualTask)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(secondEqualTask);
  advanceClock(5);
  firstEqual.finished.wait();
  secondEqual.finished.wait();
  const std::vector<int> expectedEqualOrder {100, 200, 101, 201, 102, 202};
  if (priorityOrder != expectedEqualOrder) {
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

  // The FreeRTOS tick remains millisecond based, but esp_timer deadlines are
  // microsecond based and must not be rounded by the scheduler clock API.
  setClockMicros(0);
  subMillisecondTimerFired.store(false);
  const esp_timer_create_args_t subMillisecondArgs = {subMillisecondTimerCallback, nullptr,
                                                      ESP_TIMER_TASK, "sub-ms", false};
  esp_timer_handle_t subMillisecondTimer = nullptr;
  if (esp_timer_create(&subMillisecondArgs, &subMillisecondTimer) != ESP_OK
      || esp_timer_start_once(subMillisecondTimer, 1500) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClockMicros(1499);
  if (subMillisecondTimerFired.load()) {
    return fail(__LINE__);
  }
  advanceClockMicros(1);
  waitFor(subMillisecondTimerFired);
  if (!subMillisecondTimerFired.load() || clockMicros() != 1500) {
    return fail(__LINE__);
  }
  esp_timer_delete(subMillisecondTimer);

  // The timer service callback and a task released at the same virtual tick
  // share the scheduler gate. The modeled timer-service priority wins before
  // the task runs, and the callback is never invoked concurrently.
  if (ESP_TASK_TIMER_PRIO != configMAX_PRIORITIES - 3 || ESP_TASK_TIMER_PRIO <= kControlPriority) {
    return fail(__LINE__);
  }
  setClockMillis(0);
  timerOrder.clear();
  subMillisecondTimerFired.store(false);
  TimerTaskState timerTaskState;
  TaskHandle_t timerOrderTaskHandle = nullptr;
  if (xTaskCreate(timerOrderTask, "timer-order-task", 0, &timerTaskState, kControlPriority,
                  &timerOrderTaskHandle)
      != pdPASS) {
    return fail(__LINE__);
  }
  waitForBlocked(timerOrderTaskHandle);
  esp_timer_handle_t orderedTimer = nullptr;
  const esp_timer_create_args_t orderedTimerArgs = {subMillisecondTimerCallback, nullptr,
                                                    ESP_TIMER_TASK, "timer-order", false};
  if (esp_timer_create(&orderedTimerArgs, &orderedTimer) != ESP_OK
      || esp_timer_start_once(orderedTimer, 5000) != ESP_OK) {
    return fail(__LINE__);
  }
  advanceClock(5);
  waitFor(subMillisecondTimerFired);
  timerTaskState.finished.wait();
  if (timerOrder != std::vector<int> {1, 2}) {
    return fail(__LINE__);
  }
  esp_timer_delete(orderedTimer);
  furble_sim_stop_all_tasks();
  furble_sim_reset_tasks();

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
  if (xTaskCreate(blockingDelayTask, "delete-delay", 0, &blockingDelay, 0, &blockingDelayTaskHandle)
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
  if (xTaskCreate(deleteOtherTask, "delete-other", 0, &deleteOther, 0, &deleteOtherTaskHandle)
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
    if (xTaskCreate(stopRaceTask, "stop-race", 0, &stopRace, 0, &stopRaceTaskHandle) != pdPASS) {
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
        || stopReturned.load() || furble_sim_task_join_claimed(stopRaceTaskHandle)) {
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
