#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*)(void *),
                       const char *,
                       uint32_t,
                       void *,
                       UBaseType_t,
                       TaskHandle_t *);
BaseType_t xTaskNotify(TaskHandle_t, uint32_t, int);
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, TickType_t);
constexpr int eSetBits = 0;
