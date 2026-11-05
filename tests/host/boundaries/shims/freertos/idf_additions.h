#ifndef FURBLE_BOUNDARY_FREERTOS_IDF_ADDITIONS_H
#define FURBLE_BOUNDARY_FREERTOS_IDF_ADDITIONS_H

#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t xTaskCreateWithCaps(TaskFunction_t task,
                               const char *name,
                               uint32_t stack_size,
                               void *parameter,
                               UBaseType_t priority,
                               TaskHandle_t *task_handle,
                               uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif
