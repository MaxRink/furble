#ifndef FURBLE_BOUNDARY_DRIVER_GPIO_H
#define FURBLE_BOUNDARY_DRIVER_GPIO_H

#include <cstdint>

using gpio_num_t = int;

constexpr gpio_num_t GPIO_NUM_NC = -1;
constexpr gpio_num_t GPIO_NUM_10 = 10;
constexpr gpio_num_t GPIO_NUM_19 = 19;

enum gpio_mode_t {
  GPIO_MODE_DISABLE = 0,
  GPIO_MODE_INPUT = 1,
  GPIO_MODE_OUTPUT = 2,
};

enum gpio_level_t {
  GPIO_LEVEL_LOW = 0,
  GPIO_LEVEL_HIGH = 1,
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
};

struct gpio_config_t {
  uint64_t pin_bit_mask;
  gpio_mode_t mode;
  gpio_pullup_t pull_up_en;
  gpio_pulldown_t pull_down_en;
  gpio_int_type_t intr_type;
};

int gpio_config(const gpio_config_t *config);
int gpio_install_isr_service(int flags);
int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
int gpio_set_level(gpio_num_t pin, int level);

#endif
