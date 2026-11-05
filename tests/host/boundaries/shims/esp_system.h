#ifndef FURBLE_BOUNDARY_ESP_SYSTEM_H
#define FURBLE_BOUNDARY_ESP_SYSTEM_H

#include <cstdint>

#define ESP_RST_POWERON 1
#define ESP_RST_SW 3
#define ESP_RST_BROWNOUT 7

inline void esp_restart(void) {}

#endif
