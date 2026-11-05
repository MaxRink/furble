#ifndef FURBLE_HOST_CRASH_ESP_TIMER_H
#define FURBLE_HOST_CRASH_ESP_TIMER_H

#include <cstdint>

inline int64_t &hostCrashNowUs(void) {
  static int64_t now = 0;
  return now;
}

inline int64_t esp_timer_get_time(void) {
  return hostCrashNowUs();
}

#endif
