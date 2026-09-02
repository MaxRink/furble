// Host FreeRTOS queue shim. See freertos/FreeRTOS.h for the rationale.
#ifndef INC_QUEUE_H
#define INC_QUEUE_H

#include "freertos/FreeRTOS.h"

struct FurbleHostQueue;
typedef struct FurbleHostQueue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t ticks_to_wait);
BaseType_t xQueueReset(QueueHandle_t queue);

#endif
