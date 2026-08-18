#ifndef FURBLE_SIM_ESP_TIMER_H
#define FURBLE_SIM_ESP_TIMER_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <esp_err.h>

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
      : callback {args.callback}, arg {args.arg}, worker {[this]() { run(); }} {}

  ~FurbleSimTimer() {
    {
      const std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      active = false;
      generation++;
    }
    condition.notify_one();
    worker.join();
  }

  void run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopping) {
      condition.wait(lock, [this]() { return stopping || active; });
      if (stopping) {
        break;
      }

      const uint64_t currentGeneration = generation;
      const auto deadline = nextDeadline;
      if (condition.wait_until(lock, deadline, [this, currentGeneration]() {
            return stopping || !active || generation != currentGeneration;
          })) {
        continue;
      }

      active = false;
      lock.unlock();
      callback(arg);
      lock.lock();
    }
  }

  esp_timer_cb_t callback;
  void *arg;
  std::mutex mutex;
  std::condition_variable condition;
  std::thread worker;
  bool stopping = false;
  bool active = false;
  uint64_t generation = 0;
  std::chrono::steady_clock::time_point nextDeadline;
};

using esp_timer_handle_t = FurbleSimTimer *;

inline int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
}

inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                  esp_timer_handle_t *out_handle) {
  if (args == nullptr || out_handle == nullptr || args->callback == nullptr) {
    return ESP_FAIL;
  }
  *out_handle = new FurbleSimTimer(*args);
  return ESP_OK;
}

inline esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  delete timer;
  return ESP_OK;
}

inline bool esp_timer_is_active(esp_timer_handle_t timer) {
  if (timer == nullptr) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(timer->mutex);
  return timer->active;
}

inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  if (timer == nullptr) {
    return ESP_FAIL;
  }
  {
    const std::lock_guard<std::mutex> lock(timer->mutex);
    timer->active = false;
    timer->generation++;
  }
  timer->condition.notify_one();
  return ESP_OK;
}

inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
  if (timer == nullptr) {
    return ESP_FAIL;
  }
  {
    const std::lock_guard<std::mutex> lock(timer->mutex);
    timer->active = true;
    timer->generation++;
    timer->nextDeadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);
  }
  timer->condition.notify_one();
  return ESP_OK;
}

#endif
