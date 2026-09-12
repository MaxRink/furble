#ifndef FURBLE_HOST_ESP_TIMER_H
#define FURBLE_HOST_ESP_TIMER_H

#include <cstdint>

extern "C" int64_t esp_timer_get_time(void);

// Jump the mock clock forward, for suites that must cross a rate limit
// without sleeping through it. Defined by the mock NimBLE layer.
extern "C" void furble_host_advance_time(int64_t us);

#endif
