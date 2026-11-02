#ifndef FURBLE_HOST_SEMPHR_H
#define FURBLE_HOST_SEMPHR_H

#include "freertos/FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

#endif
