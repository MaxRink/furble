#ifndef FURBLE_HOST_MQTT_ESP_HEAP_CAPS_H
#define FURBLE_HOST_MQTT_ESP_HEAP_CAPS_H

#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_INTERNAL (1U << 11)
#define MALLOC_CAP_SPIRAM (1U << 10)
#define MALLOC_CAP_8BIT (1U << 2)

namespace host_mqtt_heap {
void set(size_t freeBytes, size_t largestBlock);
void reset(void);
}  // namespace host_mqtt_heap

extern "C" size_t heap_caps_get_free_size(uint32_t caps);
extern "C" size_t heap_caps_get_largest_free_block(uint32_t caps);

#endif
