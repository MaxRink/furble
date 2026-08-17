#ifndef FURBLE_SIM_ESP_HEAP_CAPS_H
#define FURBLE_SIM_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_DMA 0x01
#define MALLOC_CAP_INTERNAL 0x02

inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  void *ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return nullptr;
  }
  return ptr;
}

#endif
