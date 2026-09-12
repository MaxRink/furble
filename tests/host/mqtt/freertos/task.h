#ifndef FURBLE_HOST_MQTT_FREERTOS_TASK_H
#define FURBLE_HOST_MQTT_FREERTOS_TASK_H

#include "FreeRTOS.h"

extern "C" {
BaseType_t xTaskCreate(void (*task)(void *),
                       const char *name,
                       uint32_t stack_size,
                       void *param,
                       UBaseType_t priority,
                       TaskHandle_t *out_handle);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait);
}

#endif
