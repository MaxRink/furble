#ifndef FURBLE_HOST_TIME_KEEPER_ESP_TIMER_H
#define FURBLE_HOST_TIME_KEEPER_ESP_TIMER_H

#include <cstdint>

extern "C" int64_t esp_timer_get_time(void);

#endif
