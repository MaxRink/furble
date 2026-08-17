#ifndef FURBLE_SIM_ESP_SLEEP_H
#define FURBLE_SIM_ESP_SLEEP_H

typedef enum {
  ESP_SLEEP_WAKEUP_TIMER = 3,
} esp_sleep_source_t;

inline void esp_sleep_disable_wakeup_source(esp_sleep_source_t) {}

#endif
