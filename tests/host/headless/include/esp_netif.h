#ifndef FURBLE_HEADLESS_ESP_NETIF_H
#define FURBLE_HEADLESS_ESP_NETIF_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_netif_init(void);

#ifdef __cplusplus
}
#endif

#endif
