#ifndef FURBLE_CONTROL_E2E_FREERTOS_SEMPHR_H
#define FURBLE_CONTROL_E2E_FREERTOS_SEMPHR_H

#include "FreeRTOS.h"

struct FurbleHostSemaphore;
using SemaphoreHandle_t = FurbleHostSemaphore *;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

#endif
