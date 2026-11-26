#ifndef FURBLE_PLATFORM_TEST_ESP_TIMER_H
#define FURBLE_PLATFORM_TEST_ESP_TIMER_H

#include <cstdint>

// The microsecond clock src/FurblePower.cpp reads. The test does not depend on
// its value, so it is a fixed monotonic counter.
inline int64_t esp_timer_get_time(void) {
  return 0;
}

#endif  // FURBLE_PLATFORM_TEST_ESP_TIMER_H
