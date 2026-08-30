#ifndef FURBLE_SIM_ESP_TIMER_H
#define FURBLE_SIM_ESP_TIMER_H

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <esp_err.h>

#include "clock.h"

typedef void (*esp_timer_cb_t)(void *arg);

typedef enum {
  ESP_TIMER_TASK,
} esp_timer_dispatch_t;

typedef struct {
  esp_timer_cb_t callback;
  void *arg;
  esp_timer_dispatch_t dispatch_method;
  const char *name;
  bool skip_unhandled_events;
} esp_timer_create_args_t;

struct FurbleSimTimer {
  explicit FurbleSimTimer(const esp_timer_create_args_t &args)
      : callback {args.callback}, arg {args.arg} {}

  esp_timer_cb_t callback;
  void *arg;
  bool active = false;
  bool callback_running = false;
  bool delete_requested = false;
  uint64_t generation = 0;
  uint64_t arm_sequence = 0;
  uint64_t next_deadline = 0;
};

using esp_timer_handle_t = FurbleSimTimer *;

inline std::vector<FurbleSimTimer *> &furbleSimTimerRegistry(void) {
  static std::vector<FurbleSimTimer *> timers;
  return timers;
}

inline std::thread &furbleSimTimerDispatcher(void) {
  static std::thread dispatcher;
  return dispatcher;
}

inline bool &furbleSimTimerDispatcherStopping(void) {
  static bool stopping = false;
  return stopping;
}

inline uint64_t &furbleSimTimerArmSequence(void) {
  static uint64_t sequence = 0;
  return sequence;
}

inline FurbleSimTimer *furbleSimNextDueTimerLocked(void) {
  const uint64_t now = Furble::Sim::clockMicros();
  FurbleSimTimer *next = nullptr;
  for (FurbleSimTimer *candidate : furbleSimTimerRegistry()) {
    if (!candidate->active || candidate->next_deadline > now) {
      continue;
    }
    if (next == nullptr || candidate->next_deadline < next->next_deadline
        || (candidate->next_deadline == next->next_deadline
            && candidate->arm_sequence < next->arm_sequence)) {
      next = candidate;
    }
  }
  return next;
}

inline void furbleSimEraseTimerLocked(FurbleSimTimer *timer) {
  auto &timers = furbleSimTimerRegistry();
  timers.erase(std::remove(timers.begin(), timers.end(), timer), timers.end());
}

inline void furbleSimTimerDispatcherRun(void) {
  std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
  for (;;) {
    Furble::Sim::schedulerCondition().wait(lock, []() {
      return furbleSimTimerDispatcherStopping() || Furble::Sim::schedulerStopping()
             || furbleSimNextDueTimerLocked() != nullptr;
    });
    if (furbleSimTimerDispatcherStopping() || Furble::Sim::schedulerStopping()) {
      return;
    }

    FurbleSimTimer *timer = furbleSimNextDueTimerLocked();
    if (timer == nullptr) {
      continue;
    }
    timer->active = false;
    timer->callback_running = true;
    lock.unlock();
    timer->callback(timer->arg);
    lock.lock();
    timer->callback_running = false;
    if (timer->delete_requested) {
      furbleSimEraseTimerLocked(timer);
      lock.unlock();
      delete timer;
      lock.lock();
    }
    Furble::Sim::schedulerCondition().notify_all();
  }
}

inline void furbleSimEnsureTimerDispatcherLocked(void) {
  if (furbleSimTimerDispatcher().joinable()) {
    return;
  }
  furbleSimTimerDispatcherStopping() = false;
  furbleSimTimerDispatcher() = std::thread(furbleSimTimerDispatcherRun);
}

inline void furble_sim_stop_all_timers(void) {
  std::thread dispatcher;
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    furbleSimTimerDispatcherStopping() = true;
    for (FurbleSimTimer *timer : furbleSimTimerRegistry()) {
      timer->active = false;
      timer->generation++;
    }
    if (furbleSimTimerDispatcher().joinable()) {
      dispatcher = std::move(furbleSimTimerDispatcher());
    }
  }
  Furble::Sim::schedulerCondition().notify_all();
  if (dispatcher.joinable() && dispatcher.get_id() != std::this_thread::get_id()) {
    dispatcher.join();
  }
}

inline int64_t esp_timer_get_time(void) {
  return static_cast<int64_t>(Furble::Sim::clockMicros());
}

inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                  esp_timer_handle_t *out_handle) {
  if (args == nullptr || out_handle == nullptr || args->callback == nullptr) {
    return ESP_FAIL;
  }

  FurbleSimTimer *timer = new FurbleSimTimer(*args);
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (Furble::Sim::schedulerStopping()) {
      delete timer;
      return ESP_FAIL;
    }
    furbleSimTimerRegistry().push_back(timer);
    furbleSimEnsureTimerDispatcherLocked();
  }
  *out_handle = timer;
  return ESP_OK;
}

inline esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  if (timer == nullptr) {
    return ESP_FAIL;
  }

  {
    std::unique_lock<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (timer->active) {
      return ESP_ERR_INVALID_STATE;
    }
    if (timer->callback_running) {
      timer->delete_requested = true;
      return ESP_OK;
    }
    furbleSimEraseTimerLocked(timer);
  }
  delete timer;
  return ESP_OK;
}

inline bool esp_timer_is_active(esp_timer_handle_t timer) {
  if (timer == nullptr) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
  return timer->active;
}

inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  if (timer == nullptr || Furble::Sim::schedulerStopping()) {
    return ESP_FAIL;
  }
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (!timer->active) {
      return ESP_ERR_INVALID_STATE;
    }
    timer->active = false;
    timer->generation++;
  }
  Furble::Sim::schedulerCondition().notify_all();
  return ESP_OK;
}

inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
  if (timer == nullptr || Furble::Sim::schedulerStopping()) {
    return ESP_FAIL;
  }
  {
    const std::lock_guard<std::mutex> lock(Furble::Sim::schedulerMutex());
    if (timer->active || timer->delete_requested) {
      return ESP_ERR_INVALID_STATE;
    }
    timer->active = true;
    timer->generation++;
    timer->arm_sequence = ++furbleSimTimerArmSequence();
    timer->next_deadline = Furble::Sim::clockMicros() + timeout_us;
    furbleSimEnsureTimerDispatcherLocked();
  }
  Furble::Sim::schedulerCondition().notify_all();
  return ESP_OK;
}

#endif
