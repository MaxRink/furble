#pragma once
#include <cstdint>
struct timeval;
struct esp_sntp_config_t {
  bool start;
  bool smooth_sync;
  void (*sync_cb)(timeval *);
  const char *server;
};
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server_) \
  esp_sntp_config_t {                          \
    true, false, nullptr, server_              \
  }
int esp_netif_sntp_init(const esp_sntp_config_t *);
int esp_netif_sntp_start(void);
void esp_netif_sntp_deinit(void);
