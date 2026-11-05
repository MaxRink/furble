#ifndef FURBLE_BOUNDARY_ESP_ETH_H
#define FURBLE_BOUNDARY_ESP_ETH_H

#include <cstdint>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

struct esp_eth_mac_t {
  esp_err_t (*del)(esp_eth_mac_t *mac) = nullptr;
};

struct esp_eth_phy_t {
  esp_err_t (*del)(esp_eth_phy_t *phy) = nullptr;
};

struct esp_eth_handle;
using esp_eth_handle_t = esp_eth_handle *;

struct eth_mac_config_t {
  int unused = 0;
};

struct eth_phy_config_t {
  int reset_gpio_num = -1;
};

struct esp_eth_config_t {
  esp_eth_mac_t *mac = nullptr;
  esp_eth_phy_t *phy = nullptr;
};

constexpr int ETH_CMD_S_MAC_ADDR = 1;

inline eth_mac_config_t ETH_MAC_DEFAULT_CONFIG() {
  return eth_mac_config_t {};
}

inline eth_phy_config_t ETH_PHY_DEFAULT_CONFIG() {
  return eth_phy_config_t {};
}

inline esp_eth_config_t ETH_DEFAULT_CONFIG(esp_eth_mac_t *mac, esp_eth_phy_t *phy) {
  esp_eth_config_t config;
  config.mac = mac;
  config.phy = phy;
  return config;
}

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *handle);
esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t handle);
esp_err_t esp_eth_start(esp_eth_handle_t handle);
esp_err_t esp_eth_stop(esp_eth_handle_t handle);
esp_err_t esp_eth_ioctl(esp_eth_handle_t handle, int command, void *data);
esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t handle);
esp_err_t esp_eth_del_netif_glue(esp_eth_netif_glue_handle_t glue);

#ifdef __cplusplus
}
#endif

#endif
