#ifndef FURBLE_SIM_ESP_EVENT_H
#define FURBLE_SIM_ESP_EVENT_H

#include <cstdint>

#include "esp_err.h"

using esp_event_base_t = const char *;
using esp_event_handler_t = void (*)(void *, esp_event_base_t, int32_t, void *);

inline esp_err_t esp_event_loop_create_default(void) {
  return ESP_OK;
}

#endif
