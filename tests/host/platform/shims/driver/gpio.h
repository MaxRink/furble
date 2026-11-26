#ifndef FURBLE_PLATFORM_TEST_DRIVER_GPIO_H
#define FURBLE_PLATFORM_TEST_DRIVER_GPIO_H

#include <cstdint>

#include "esp_err.h"

// The GPIO boundary for the real src/FurblePlatform.cpp. The wake pin is never
// the StickS3 wake source (the PMIC drives it), so these record nothing beyond
// accepting the call. The PMIC path is modelled in sim/shim/M5PM1.h.
//
// gpio_num_t is an enum, as it is in the IDF. An integer alias here makes the
// production `static_cast<gpio_num_t>(int8_t)` profile pin reads look like a
// signed-char to int conversion, which clang-tidy reports.
enum gpio_num_t {
  GPIO_NUM_NC = -1,
  GPIO_NUM_0 = 0,
  GPIO_NUM_4 = 4,
  GPIO_NUM_10 = 10,
  GPIO_NUM_13 = 13,
  GPIO_NUM_19 = 19,
  GPIO_NUM_35 = 35,
  GPIO_NUM_46 = 46,
};

enum gpio_mode_t {
  GPIO_MODE_DISABLE = 0,
  GPIO_MODE_INPUT = 1,
  GPIO_MODE_OUTPUT = 2,
};

enum gpio_pullup_t {
  GPIO_PULLUP_DISABLE = 0,
  GPIO_PULLUP_ENABLE = 1,
};

enum gpio_pulldown_t {
  GPIO_PULLDOWN_DISABLE = 0,
  GPIO_PULLDOWN_ENABLE = 1,
};

enum gpio_int_type_t {
  GPIO_INTR_DISABLE = 0,
  GPIO_INTR_POSEDGE = 1,
  GPIO_INTR_NEGEDGE = 2,
  GPIO_INTR_ANYEDGE = 3,
  GPIO_INTR_LOW_LEVEL = 4,
  GPIO_INTR_HIGH_LEVEL = 5,
};

typedef struct {
  uint64_t pin_bit_mask;
  gpio_mode_t mode;
  gpio_pullup_t pull_up_en;
  gpio_pulldown_t pull_down_en;
  gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *config);
int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
int gpio_set_level(gpio_num_t pin, int level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type);
esp_err_t gpio_wakeup_disable(gpio_num_t pin);

#endif  // FURBLE_PLATFORM_TEST_DRIVER_GPIO_H
