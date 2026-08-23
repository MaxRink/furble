#ifndef FURBLE_COMPANION_HOST_ESP_TIMER_H
#define FURBLE_COMPANION_HOST_ESP_TIMER_H

#include <cstdint>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;

enum esp_timer_dispatch_t {
  ESP_TIMER_TASK = 0,
};

using esp_timer_cb_t = void (*)(void *);

struct esp_timer_create_args_t {
  esp_timer_cb_t callback;
  void *arg;
  esp_timer_dispatch_t dispatch_method;
  const char *name;
  bool skip_unhandled_events;
};

struct FurbleHostTimer;
using esp_timer_handle_t = FurbleHostTimer *;

extern "C" int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle);
esp_err_t esp_timer_delete(esp_timer_handle_t handle);
bool esp_timer_is_active(esp_timer_handle_t handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);

#endif
