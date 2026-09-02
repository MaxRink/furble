// Host FreeRTOS shim for the console command suite.
//
// A std::thread, std::mutex and std::condition_variable model of the slice of
// FreeRTOS that src/FurbleConsole.cpp and src/FurbleControl.cpp use: counted
// queues, tasks, and the task run time statistics the 'perf tasks' command
// reports. Implemented in console_shim.cpp.
//
// The header path matches the device include, freertos/FreeRTOS.h, so the real
// production include graph resolves here on the host build.
#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H

#include <cstddef>
#include <cstdint>

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

#define portMAX_DELAY ((TickType_t)0xffffffffUL)

// One tick models one millisecond on the host.
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#include "freertos/queue.h"
#include "freertos/task.h"

#endif
