#ifndef FURBLE_HEADLESS_ESP_EVENT_H
#define FURBLE_HEADLESS_ESP_EVENT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_event_loop_create_default(void);

#ifdef __cplusplus
}
#endif

#endif
