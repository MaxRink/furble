#ifndef FURBLE_BOUNDARY_ESP_NETIF_H
#define FURBLE_BOUNDARY_ESP_NETIF_H

#include <cstdint>

#include "esp_err.h"

struct esp_ip4_addr_t {
  uint32_t addr = 0;
};

struct esp_netif_ip_info_t {
  esp_ip4_addr_t ip;
  esp_ip4_addr_t netmask;
  esp_ip4_addr_t gw;
};

struct esp_netif_t {
  int id = 0;
  bool attached = false;
};

struct esp_netif_config_t {
  int unused = 0;
};

struct esp_eth_netif_glue_t {
  int id = 0;
};
using esp_eth_netif_glue_handle_t = esp_eth_netif_glue_t *;

constexpr int ETHERNET_EVENT_CONNECTED = 1;
constexpr int ETHERNET_EVENT_DISCONNECTED = 2;
constexpr int IP_EVENT_ETH_GOT_IP = 3;
constexpr int IP_EVENT_ETH_LOST_IP = 4;

struct ip_event_got_ip_t {
  esp_netif_ip_info_t ip_info;
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_new(const esp_netif_config_t *config);
void esp_netif_destroy(esp_netif_t *netif);
esp_err_t esp_netif_attach(esp_netif_t *netif, esp_eth_netif_glue_handle_t glue);
const char *esp_ip4addr_ntoa(const esp_ip4_addr_t *address, char *buffer, int length);

#ifdef __cplusplus
}
#endif

inline esp_netif_config_t ESP_NETIF_DEFAULT_ETH() {
  return esp_netif_config_t {};
}

#endif
