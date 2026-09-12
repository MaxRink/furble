#ifndef FURBLE_HOST_MQTT_FREERTOS_QUEUE_H
#define FURBLE_HOST_MQTT_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

extern "C" {
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
}

#endif
