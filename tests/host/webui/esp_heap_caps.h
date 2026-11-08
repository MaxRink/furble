#ifndef FURBLE_HOST_WEBUI_ESP_HEAP_CAPS_H
#define FURBLE_HOST_WEBUI_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1U << 11;
constexpr uint32_t MALLOC_CAP_SPIRAM = 1U << 10;
constexpr uint32_t MALLOC_CAP_8BIT = 1U << 2;

extern "C" size_t heap_caps_get_free_size(uint32_t caps);
extern "C" size_t heap_caps_get_largest_free_block(uint32_t caps);

namespace host_webui_heap {
void set(size_t freeBytes, size_t largestBlock);
void reset();
/** Tasks the code under test created with this name. */
size_t tasksCreated(const char *name);
}  // namespace host_webui_heap

#endif
