#ifndef FURBLE_HOST_FREERTOS_H
#define FURBLE_HOST_FREERTOS_H

#include <cstdint>

using TickType_t = uint32_t;

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

inline void vTaskDelay(TickType_t) {}

#endif
