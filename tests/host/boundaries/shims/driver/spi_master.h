#ifndef FURBLE_BOUNDARY_DRIVER_SPI_MASTER_H
#define FURBLE_BOUNDARY_DRIVER_SPI_MASTER_H

#include <cstdint>

#include "esp_err.h"

using spi_host_device_t = int;

constexpr spi_host_device_t SPI2_HOST = 2;
constexpr int SPI_DMA_CH_AUTO = 1;

struct spi_bus_config_t {
  int mosi_io_num = -1;
  int miso_io_num = -1;
  int sclk_io_num = -1;
  int quadwp_io_num = -1;
  int quadhd_io_num = -1;
};

struct spi_device_interface_config_t {
  uint8_t command_bits = 0;
  uint8_t address_bits = 0;
  uint8_t mode = 0;
  int clock_speed_hz = 0;
  int spics_io_num = -1;
  int queue_size = 0;
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *config, int dma_chan);
esp_err_t spi_bus_free(spi_host_device_t host);

#ifdef __cplusplus
}
#endif

#endif
