// Host esp_timer shim. The engines only use it to rate limit polling, and the
// test drives that rate explicitly.
#ifndef FURBLE_HOST_ESP_TIMER_H
#define FURBLE_HOST_ESP_TIMER_H

#include <cstdint>

int64_t esp_timer_get_time(void);

#endif
