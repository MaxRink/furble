#ifndef FURBLE_HEADLESS_ESP_TIMER_H
#define FURBLE_HEADLESS_ESP_TIMER_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif
