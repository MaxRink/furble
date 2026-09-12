// Host FreeRTOS shim for the Control disconnect regression test.
//
// This is a standalone, host-only model of the small slice of the FreeRTOS API
// that src/FurbleControl.cpp uses: counted queues and per-target tasks. It is
// implemented with std::thread, std::mutex and std::condition_variable in
// control_shim.cpp. It carries no profiler, clock or SDL dependency, so the
// test compiles the real production Control against it directly.
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

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

#define portMAX_DELAY ((TickType_t)0xffffffffUL)

// One tick models one millisecond on the host.
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#include "freertos/queue.h"
#include "freertos/task.h"

/**
 * Stop and join every shim-backed task before host static destruction.
 *
 * The control task is immortal on device: it loops forever and has no exit
 * path. On the host the process does exit, and C++ static destruction then
 * frees the objects that thread is still using, so a suite that leaves it
 * running has to end in std::_Exit(). That skips atexit, and with it
 * __llvm_profile_write_file, so the suite never writes its counters and
 * measures nothing under coverage (issue #277).
 *
 * This sets a stop flag, wakes every blocking primitive a task can be parked
 * in, and joins the threads. It is the same contract the console and the
 * control end-to-end harnesses use. Call it, or let FurbleHostTaskScope call
 * it, before main() returns.
 */
void furbleHostStopTasks(void);

/** Scope guard that runs furbleHostStopTasks() on every exit path. */
class FurbleHostTaskScope {
 public:
  FurbleHostTaskScope() = default;
  ~FurbleHostTaskScope() { furbleHostStopTasks(); }

  FurbleHostTaskScope(const FurbleHostTaskScope &) = delete;
  FurbleHostTaskScope &operator=(const FurbleHostTaskScope &) = delete;
};

#endif
