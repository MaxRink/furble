#ifndef FURBLE_HOST_MQTT_ESP_TIMER_H
#define FURBLE_HOST_MQTT_ESP_TIMER_H

#include <cstdint>

#include "esp_err.h"

using esp_timer_cb_t = void (*)(void *);

enum esp_timer_dispatch_t {
  ESP_TIMER_TASK,
};

struct esp_timer_create_args_t {
  esp_timer_cb_t callback;
  void *arg;
  esp_timer_dispatch_t dispatch_method;
  const char *name;
  bool skip_unhandled_events;
};

struct esp_timer_stub_t;
using esp_timer_handle_t = esp_timer_stub_t *;

extern "C" {
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
}

#endif
