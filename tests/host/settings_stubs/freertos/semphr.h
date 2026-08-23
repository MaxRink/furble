#ifndef FURBLE_HOST_SETTINGS_SEMPHR_H
#define FURBLE_HOST_SETTINGS_SEMPHR_H

#include "FreeRTOS.h"

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  return nullptr;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
  return pdFALSE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
  return pdFALSE;
}

#endif
