// Host esp_timer shim. The guard matches tests/host/nimble/esp_timer.h so the
// two never declare the same symbol twice in one translation unit.
#ifndef FURBLE_HOST_ESP_TIMER_H
#define FURBLE_HOST_ESP_TIMER_H

#include <cstdint>

extern "C" int64_t esp_timer_get_time(void);

#endif
