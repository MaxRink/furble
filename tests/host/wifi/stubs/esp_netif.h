#pragma once
#include <cstdint>
struct esp_netif_t {};
struct esp_ip4_addr_t {
  uint32_t addr = 0;
};
struct esp_netif_ip_info_t {
  esp_ip4_addr_t ip;
};
esp_netif_t *esp_netif_create_default_wifi_sta(void);
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
const char *esp_ip4addr_ntoa(const esp_ip4_addr_t *, char *, int);
