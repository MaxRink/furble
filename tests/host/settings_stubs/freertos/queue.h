#ifndef FURBLE_HOST_SETTINGS_QUEUE_H
#define FURBLE_HOST_SETTINGS_QUEUE_H

#include "FreeRTOS.h"

inline QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t) {
  return nullptr;
}

inline BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t) {
  return pdFALSE;
}

inline BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t) {
  return pdFALSE;
}

#endif
