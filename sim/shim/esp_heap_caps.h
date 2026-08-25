#ifndef FURBLE_SIM_ESP_HEAP_CAPS_H
#define FURBLE_SIM_ESP_HEAP_CAPS_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#else
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#endif

#define MALLOC_CAP_DMA 0x01
#define MALLOC_CAP_INTERNAL 0x02

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t capabilities) {
  (void)capabilities;
  void *ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return NULL;
  }
  return ptr;
}

#endif
