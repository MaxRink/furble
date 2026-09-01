// Host esp_heap_caps shim for the console command suite.
//
// The heap dumps only need a stable, non-zero snapshot per capability so the
// 'perf heap' and 'debug heap' output lines are exercised and assertable.
#ifndef FURBLE_HOST_CONSOLE_ESP_HEAP_CAPS_H
#define FURBLE_HOST_CONSOLE_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_INTERNAL (1 << 0)
#define MALLOC_CAP_DMA (1 << 1)
#define MALLOC_CAP_SPIRAM (1 << 2)

typedef struct {
  size_t total_free_bytes;
  size_t total_allocated_bytes;
  size_t largest_free_block;
  size_t minimum_free_bytes;
  size_t allocated_blocks;
  size_t free_blocks;
  size_t total_blocks;
} multi_heap_info_t;

void heap_caps_get_info(multi_heap_info_t *info, uint32_t capabilities);

#endif
