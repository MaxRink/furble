#ifndef FURBLE_SIM_ESP_NETIF_H
#define FURBLE_SIM_ESP_NETIF_H

#include <cstdint>

#include "esp_err.h"

struct esp_netif_t {
  bool up = true;
  uint32_t ip = 0x0100007f;
};

struct esp_netif_ip_info_t {
  struct {
    uint32_t addr;
  } ip;
};

inline esp_netif_t *furble_sim_netif(void) {
  static esp_netif_t netif;
  return &netif;
}

inline esp_err_t esp_netif_init(void) {
  return ESP_OK;
}

inline esp_netif_t *esp_netif_next_unsafe(esp_netif_t *netif) {
  return netif == nullptr ? furble_sim_netif() : nullptr;
}

inline bool esp_netif_is_netif_up(esp_netif_t *netif) {
  return netif != nullptr && netif->up;
}

inline esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
  if (netif == nullptr || info == nullptr) {
    return ESP_FAIL;
  }
  info->ip.addr = netif->ip;
  return ESP_OK;
}

#endif
