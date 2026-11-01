#ifndef FURBLE_HOST_WEBUI_FREERTOS_H
#define FURBLE_HOST_WEBUI_FREERTOS_H
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using TaskHandle_t = void *;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
#define pdMS_TO_TICKS(ms) (ms)
#endif
