#ifndef FURBLE_HOST_SETTINGS_SDMMC_CMD_H
#define FURBLE_HOST_SETTINGS_SDMMC_CMD_H

#include <cstdint>

using gpio_num_t = int;
using spi_host_device_t = int;

struct sdmmc_csd_t {
  uint32_t capacity = 0;
  uint32_t sector_size = 0;
};

struct sdmmc_card_t {
  sdmmc_csd_t csd;
};

struct sdmmc_host_t {
  int slot = 0;
  int max_freq_khz = 0;
};

struct sdspi_device_config_t {
  gpio_num_t gpio_cs = -1;
  spi_host_device_t host_id = 0;
};

#define SDSPI_HOST_DEFAULT() \
  ::sdmmc_host_t {}
#define SDSPI_DEVICE_CONFIG_DEFAULT() \
  ::sdspi_device_config_t {}

#endif
