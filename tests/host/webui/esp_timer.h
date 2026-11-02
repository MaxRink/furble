#ifndef FURBLE_HOST_WEBUI_ESP_TIMER_H
#define FURBLE_HOST_WEBUI_ESP_TIMER_H
#include <cstdint>
#include "esp_err.h"
using esp_timer_cb_t = void (*)(void *);
enum esp_timer_dispatch_t { ESP_TIMER_TASK };
struct esp_timer_create_args_t {
  esp_timer_cb_t callback;
  void *arg;
  esp_timer_dispatch_t dispatch_method;
  const char *name;
  bool skip_unhandled_events;
};
struct host_webui_timer_t;
using esp_timer_handle_t = host_webui_timer_t *;
extern "C" int64_t esp_timer_get_time();
extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t);
extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t);
extern "C" bool esp_timer_is_active(esp_timer_handle_t);
namespace host_webui_timer {
void reset();
void advance(uint64_t microseconds);
void elapseAndQueue(uint64_t microseconds);
void dispatchQueued();
void setStartAdvance(uint64_t microseconds);
}  // namespace host_webui_timer
#endif
