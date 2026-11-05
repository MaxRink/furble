#ifndef FURBLE_BOUNDARY_DRIVER_SDSPI_HOST_H
#define FURBLE_BOUNDARY_DRIVER_SDSPI_HOST_H

#include <cstdint>

#include "driver/gpio.h"

using spi_host_device_t = int;

struct sdmmc_host_t {
  int slot = 1;
  int max_freq_khz = 10000;
};

struct sdspi_device_config_t {
  gpio_num_t gpio_cs = GPIO_NUM_NC;
  int host_id = 1;
};

inline sdmmc_host_t SDSPI_HOST_DEFAULT() {
  return sdmmc_host_t {};
}

inline sdspi_device_config_t SDSPI_DEVICE_CONFIG_DEFAULT() {
  return sdspi_device_config_t {};
}

#endif
