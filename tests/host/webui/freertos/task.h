#ifndef FURBLE_HOST_WEBUI_TASK_H
#define FURBLE_HOST_WEBUI_TASK_H
#include "FreeRTOS.h"
extern "C" BaseType_t xTaskCreate(void (*)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *);
extern "C" BaseType_t xTaskNotifyGive(TaskHandle_t);
extern "C" uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
#endif
