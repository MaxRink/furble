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
 *
 * Every value below is the one the pinned ESP-IDF defines, copied from
 * components/heap/include/esp_heap_caps.h lines 29 to 51 of framework-espidf
 * 3.50503.0 (ESP-IDF 5.5.3, pinned at platformio.ini line 13). They are hex
 * rather than (1 << n) so a log line prints the value the device prints.
 * tests/host/sim_heap_caps_test.cpp pins each one against those lines.
 */
#define MALLOC_CAP_EXEC 0x00000001          /* 1<<0, run executable code */
#define MALLOC_CAP_32BIT 0x00000002         /* 1<<1, aligned 32-bit access */
#define MALLOC_CAP_8BIT 0x00000004          /* 1<<2, 8/16/...-bit access */
#define MALLOC_CAP_DMA 0x00000008           /* 1<<3, DMA-reachable */
#define MALLOC_CAP_PID2 0x00000010          /* 1<<4, PID2 space, unused by IDF */
#define MALLOC_CAP_PID3 0x00000020          /* 1<<5, PID3 space, unused by IDF */
#define MALLOC_CAP_PID4 0x00000040          /* 1<<6, PID4 space, unused by IDF */
#define MALLOC_CAP_PID5 0x00000080          /* 1<<7, PID5 space, unused by IDF */
#define MALLOC_CAP_PID6 0x00000100          /* 1<<8, PID6 space, unused by IDF */
#define MALLOC_CAP_PID7 0x00000200          /* 1<<9, PID7 space, unused by IDF */
#define MALLOC_CAP_SPIRAM 0x00000400        /* 1<<10, in SPI RAM */
#define MALLOC_CAP_INTERNAL 0x00000800      /* 1<<11, in internal RAM */
#define MALLOC_CAP_DEFAULT 0x00001000       /* 1<<12, returned by malloc() */
#define MALLOC_CAP_IRAM_8BIT 0x00002000     /* 1<<13, IRAM with unaligned access */
#define MALLOC_CAP_RETENTION 0x00004000     /* 1<<14, retention-DMA reachable */
#define MALLOC_CAP_RTCRAM 0x00008000        /* 1<<15, in RTC fast memory */
#define MALLOC_CAP_TCM 0x00010000           /* 1<<16, in TCM memory */
#define MALLOC_CAP_DMA_DESC_AHB 0x00020000  /* 1<<17, AHB DMA descriptors */
#define MALLOC_CAP_DMA_DESC_AXI 0x00040000  /* 1<<18, AXI DMA descriptors */
#define MALLOC_CAP_CACHE_ALIGNED 0x00080000 /* 1<<19, cache-line aligned */
#define MALLOC_CAP_SIMD 0x00100000          /* 1<<20, SIMD data access */
#define MALLOC_CAP_INVALID 0x80000000       /* 1<<31, unusable, list end marker */

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
 * MALLOC_CAP_DEFAULT without MALLOC_CAP_SPIRAM charges the internal pool first
 * and PSRAM only when internal RAM cannot fit the block, which is the priority
 * order of the ESP32-S3 capability table in the pinned SDK (see capsPools() in
 * sim/heap_model.cpp).
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
