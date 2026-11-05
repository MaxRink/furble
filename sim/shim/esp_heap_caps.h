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

/*
 * Use the ESP-IDF capability bit values rather than opaque host tokens. The
 * internal 8-bit pair then prints as 0x804 in the modelled allocation-failure
 * line, which is the value a StickS3 panic reports on hardware.
 */
#define MALLOC_CAP_8BIT 0x0004
#define MALLOC_CAP_DMA 0x0008
#define MALLOC_CAP_SPIRAM 0x0400
#define MALLOC_CAP_INTERNAL 0x0800

#ifdef __cplusplus
extern "C" {
#endif

size_t furble_sim_heap_free(uint32_t capabilities);
size_t furble_sim_heap_largest(uint32_t capabilities);
size_t furble_sim_heap_minimum(uint32_t capabilities);

#ifdef __cplusplus
}
#endif

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t capabilities) {
  (void)capabilities;
  void *ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return NULL;
  }
  return ptr;
}

static inline size_t heap_caps_get_free_size(uint32_t capabilities) {
  return furble_sim_heap_free(capabilities);
}

static inline size_t heap_caps_get_largest_free_block(uint32_t capabilities) {
  return furble_sim_heap_largest(capabilities);
}

static inline size_t heap_caps_get_minimum_free_size(uint32_t capabilities) {
  return furble_sim_heap_minimum(capabilities);
}

#endif
