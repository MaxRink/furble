// Host esp_heap_trace double for the console command suite.
//
// The console's 'heap trace' command drives the standalone tracer. The double
// records each call in ConsoleHost::heapTrace() and returns the error a test
// sets there, so the command's admission, failure and dump paths all run.
#ifndef FURBLE_HOST_CONSOLE_ESP_HEAP_TRACE_H
#define FURBLE_HOST_CONSOLE_ESP_HEAP_TRACE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

typedef struct {
  uint32_t ccount;
  void *address;
  size_t size;
  void *alloced_by[2];
  void *freed_by[2];
} heap_trace_record_t;

typedef enum {
  HEAP_TRACE_ALL,
  HEAP_TRACE_LEAKS,
} heap_trace_mode_t;

esp_err_t heap_trace_init_standalone(heap_trace_record_t *record_buffer, size_t num_records);
esp_err_t heap_trace_start(heap_trace_mode_t mode);
esp_err_t heap_trace_stop(void);
size_t heap_trace_get_count(void);
void heap_trace_dump(void);

#endif
