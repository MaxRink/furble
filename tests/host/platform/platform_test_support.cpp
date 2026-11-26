// Host peripheral doubles for the platform target, which compiles the real
// src/FurblePlatform.cpp. Only the lowest boundary is replaced here: the M5
// singleton, the ESP GPIO and light-sleep calls, the microsecond clock, the
// LVGL timer calls the feedback driver needs to link, and the boot-timeline
// marks. The PMIC the wake path talks to is sim/shim/M5PM1.h.

#include <cstdint>

#include "FurbleBootTimeline.h"
#include "M5Unified.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "lvgl.h"

m5::M5Unified M5;

const char *LOG_TAG = "platform-wake";

namespace Furble {

// Boot marks are not the subject of this target; the timeline has its own host
// test.
void BootTimeline::mark(mark_t mark) {
  (void)mark;
}

}  // namespace Furble

esp_err_t gpio_config(const gpio_config_t *config) {
  return config == nullptr ? ESP_ERR_INVALID_ARG : ESP_OK;
}

int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) {
  (void)pin;
  (void)mode;
  return ESP_OK;
}

int gpio_set_level(gpio_num_t pin, int level) {
  (void)pin;
  (void)level;
  return ESP_OK;
}

int gpio_get_level(gpio_num_t pin) {
  (void)pin;
  return 1;
}

esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type) {
  (void)pin;
  (void)type;
  return ESP_OK;
}

esp_err_t gpio_wakeup_disable(gpio_num_t pin) {
  (void)pin;
  return ESP_OK;
}

// The feedback driver creates an LVGL timer for its patterns. Nothing here runs
// that timer: the wake path under test does not depend on feedback timing.
lv_timer_t *lv_timer_create(lv_timer_cb_t callback, uint32_t period_ms, void *user_data) {
  (void)callback;
  (void)period_ms;
  (void)user_data;
  return nullptr;
}

void lv_timer_resume(lv_timer_t *timer) {
  (void)timer;
}

void lv_timer_pause(lv_timer_t *timer) {
  (void)timer;
}

void *lv_timer_get_user_data(lv_timer_t *timer) {
  (void)timer;
  return nullptr;
}
