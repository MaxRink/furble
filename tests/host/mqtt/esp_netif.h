#ifndef FURBLE_HOST_MQTT_ESP_NETIF_H
#define FURBLE_HOST_MQTT_ESP_NETIF_H

#include <cstdint>

#include "esp_err.h"

struct esp_netif_t {
  bool up;
  uint32_t ip;
};

struct esp_netif_ip_info_t {
  struct {
    uint32_t addr;
  } ip;
};

namespace host_mqtt_network {

void reset(void);
void setGotIp(const char *transport, uint32_t address);

}  // namespace host_mqtt_network

extern "C" {
esp_netif_t *esp_netif_next_unsafe(esp_netif_t *netif);
bool esp_netif_is_netif_up(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
}

#endif
