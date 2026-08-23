#ifndef FURBLE_HOST_SETTINGS_TASK_H
#define FURBLE_HOST_SETTINGS_TASK_H

#include "FreeRTOS.h"

inline BaseType_t xTaskCreate(TaskFunction_t,
                              const char *,
                              uint32_t,
                              void *,
                              UBaseType_t,
                              TaskHandle_t *) {
  return pdFALSE;
}

#endif
