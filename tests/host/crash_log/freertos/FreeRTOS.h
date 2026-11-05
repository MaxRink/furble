#ifndef FURBLE_HOST_CRASH_FREERTOS_H
#define FURBLE_HOST_CRASH_FREERTOS_H

/* The record path uses one leaf critical section. The host test is
 * single-threaded, so the section is a no-op. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux) (void)(mux)

#include "freertos/task.h"

#endif
