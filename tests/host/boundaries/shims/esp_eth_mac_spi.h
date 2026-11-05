#ifndef FURBLE_BOUNDARY_ESP_ETH_MAC_SPI_H
#define FURBLE_BOUNDARY_ESP_ETH_MAC_SPI_H

#include <driver/spi_master.h>

#include "esp_eth.h"

struct eth_w5500_config_t {
  spi_host_device_t spi_host = SPI2_HOST;
  spi_device_interface_config_t *spi_devcfg = nullptr;
  int int_gpio_num = -1;
};

inline eth_w5500_config_t ETH_W5500_DEFAULT_CONFIG(spi_host_device_t host,
                                                   spi_device_interface_config_t *config) {
  eth_w5500_config_t result;
  result.spi_host = host;
  result.spi_devcfg = config;
  return result;
}

#ifdef __cplusplus
extern "C" {
#endif

esp_eth_mac_t *esp_eth_mac_new_w5500(const eth_w5500_config_t *config,
                                     const eth_mac_config_t *mac_config);
esp_eth_phy_t *esp_eth_phy_new_w5500(const eth_phy_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
