#ifndef FURBLE_HOST_MQTT_FREERTOS_H
#define FURBLE_HOST_MQTT_FREERTOS_H

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using TaskHandle_t = void *;
using QueueHandle_t = void *;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;

#define pdMS_TO_TICKS(milliseconds) (static_cast<TickType_t>(milliseconds))

#endif
