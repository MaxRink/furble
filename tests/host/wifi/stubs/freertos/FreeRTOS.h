#pragma once
#include <cstdint>
using BaseType_t = int;
using TickType_t = uint32_t;
using UBaseType_t = unsigned;
using TaskHandle_t = void *;
constexpr BaseType_t pdPASS = 1;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;
#define pdMS_TO_TICKS(x) (x)
