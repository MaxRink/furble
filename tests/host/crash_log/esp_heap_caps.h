#ifndef FURBLE_HOST_CRASH_ESP_HEAP_CAPS_H
#define FURBLE_HOST_CRASH_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_8BIT 0x0004
#define MALLOC_CAP_INTERNAL 0x0800

inline size_t &hostCrashHeapFree(void) {
  static size_t value = 13543;
  return value;
}

inline size_t &hostCrashHeapLargest(void) {
  static size_t value = 12788;
  return value;
}

inline size_t heap_caps_get_free_size(uint32_t) {
  return hostCrashHeapFree();
}

inline size_t heap_caps_get_largest_free_block(uint32_t) {
  return hostCrashHeapLargest();
}

typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char *function_name);

/** The callback CrashLog::init() registered, so a test can fire it. */
inline esp_alloc_failed_hook_t &hostCrashFailedAllocHook(void) {
  static esp_alloc_failed_hook_t hook = nullptr;
  return hook;
}

inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback) {
  hostCrashFailedAllocHook() = callback;
  return 0;
}

#endif
