#ifndef FURBLE_SIM_ESP_HEAP_CAPS_H
#define FURBLE_SIM_ESP_HEAP_CAPS_H

#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#else
#include <stdarg.h>
#include <stdbool.h>
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
#define MALLOC_CAP_DEFAULT 0x1000

#ifdef __cplusplus
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif

typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char *function_name);

#ifdef __cplusplus
extern "C" {
#endif

size_t furble_sim_heap_free(uint32_t capabilities);
size_t furble_sim_heap_largest(uint32_t capabilities);
size_t furble_sim_heap_minimum(uint32_t capabilities);
/* True when the modelled pool the caps name can hold size, with block overhead. */
bool furble_sim_heap_fits(size_t size, uint32_t capabilities);
bool furble_sim_heap_should_fail(size_t bytes, const char *owner);
/* Records the failure, calls the registered hook and aborts like hardware. */
void furble_sim_heap_alloc_failed(size_t bytes, uint32_t capabilities, const char *owner);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback);
/*
 * Host allocation plus the device charge for its caps. The simulator's device
 * counter defines these; host unit targets get the plain fallbacks in
 * sim/heap_model.cpp.
 */
void *furble_sim_caps_alloc(size_t alignment, size_t size, uint32_t capabilities);
void furble_sim_caps_free(void *pointer);

#ifdef __cplusplus
}
#endif

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t capabilities) {
  if (furble_sim_heap_should_fail(size, "heap_caps")) {
    furble_sim_heap_alloc_failed(size, capabilities, "heap_caps");
    return NULL;
  }
  return furble_sim_caps_alloc(alignment, size, capabilities);
}

static inline void *heap_caps_malloc(size_t size, uint32_t capabilities) {
  return heap_caps_aligned_alloc(sizeof(void *), size, capabilities);
}

/*
 * ESP-IDF tries each caps set in order through the non-aborting allocator and
 * aborts only when every set fails. The model checks each pool before it
 * charges, because a counted allocation that does not fit aborts at once.
 * MALLOC_CAP_DEFAULT without MALLOC_CAP_SPIRAM charges the internal pool, the
 * first heap ESP-IDF tries for it.
 */
static inline void *heap_caps_malloc_prefer(size_t size, size_t num, ...) {
  va_list args;
  va_start(args, num);
  uint32_t caps = 0;
  void *pointer = NULL;
  while (num-- > 0) {
    caps = va_arg(args, uint32_t);
    if (furble_sim_heap_fits(size, caps)) {
      pointer = heap_caps_malloc(size, caps);
      break;
    }
  }
  va_end(args);
  if (pointer == NULL) {
    furble_sim_heap_alloc_failed(size, caps, "heap_caps_malloc_prefer");
  }
  return pointer;
}

static inline void heap_caps_free(void *ptr) {
  furble_sim_caps_free(ptr);
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
