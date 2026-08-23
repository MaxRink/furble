#ifndef FURBLE_HOST_SETTINGS_FREERTOS_H
#define FURBLE_HOST_SETTINGS_FREERTOS_H

#include <cstddef>
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using QueueHandle_t = void *;
using SemaphoreHandle_t = void *;
using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;

#define pdMS_TO_TICKS(milliseconds) static_cast<TickType_t>(milliseconds)

#endif
