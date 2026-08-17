#ifndef FURBLE_SIM_ESP_TIMER_H
#define FURBLE_SIM_ESP_TIMER_H

#include <chrono>

inline int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
}

#endif
