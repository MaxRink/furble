#ifndef FURBLE_BOUNDARY_ESP_EVENT_H
#define FURBLE_BOUNDARY_ESP_EVENT_H

#include <cstdint>

#include "esp_err.h"

using esp_event_base_t = const char *;
using esp_event_handler_t = void (*)(void *arg,
                                     esp_event_base_t base,
                                     int32_t id,
                                     void *data);
struct esp_event_handler_instance;
using esp_event_handler_instance_t = esp_event_handler_instance *;

constexpr int32_t ESP_EVENT_ANY_ID = -1;

extern const char *ETH_EVENT;
extern const char *IP_EVENT;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,
                                              int32_t id,
                                              esp_event_handler_t handler,
                                              void *arg,
                                              esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,
                                                int32_t id,
                                                esp_event_handler_instance_t instance);

#ifdef __cplusplus
}
#endif

#endif
