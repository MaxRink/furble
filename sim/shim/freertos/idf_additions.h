#ifndef FURBLE_SIM_FREERTOS_IDF_ADDITIONS_H
#define FURBLE_SIM_FREERTOS_IDF_ADDITIONS_H

#include "FreeRTOS.h"

/*
 * Capability-placed queue. The storage is charged to the modelled pool its
 * caps select, and a failure aborts under the board's heap abort policy.
 */
QueueHandle_t xQueueCreateWithCaps(UBaseType_t queue_length,
                                   UBaseType_t item_size,
                                   UBaseType_t caps);
void vQueueDeleteWithCaps(QueueHandle_t queue);

#endif
