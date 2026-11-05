#ifndef FURBLE_BOUNDARY_ESP_HEAP_CAPS_H
#define FURBLE_BOUNDARY_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_8BIT 0x0004
#define MALLOC_CAP_DMA 0x0008
#define MALLOC_CAP_SPIRAM 0x0400
#define MALLOC_CAP_INTERNAL 0x0800

inline void *heap_caps_malloc(size_t size, uint32_t) {
  return std::malloc(size);
}

inline void heap_caps_free(void *pointer) {
  std::free(pointer);
}

inline size_t heap_caps_get_free_size(uint32_t) {
  return 65536;
}

inline size_t heap_caps_get_largest_free_block(uint32_t) {
  return 32768;
}

#endif
