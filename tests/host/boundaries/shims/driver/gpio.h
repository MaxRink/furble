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

int gpio_install_isr_service(int flags);
int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode);
int gpio_set_level(gpio_num_t pin, int level);

#endif
