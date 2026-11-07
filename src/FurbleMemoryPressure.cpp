#include "FurbleMemoryPressure.h"

#include <atomic>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace Furble {
namespace MemoryPressure {

namespace {

constexpr const char *LOG_TAG = "memory";
constexpr int64_t TICK_PERIOD_US = 1000000;

// The governor is stepped only from the UI loop; other tasks read the level.
Governor s_Governor;
std::atomic<level_t> s_Level {NORMAL};
int64_t s_LastTickUs = 0;
bool s_Ticked = false;

}  // namespace

void tick(void) {
  const int64_t now = esp_timer_get_time();
  if (s_Ticked && ((now - s_LastTickUs) < TICK_PERIOD_US)) {
    return;
  }
  s_Ticked = true;
  s_LastTickUs = now;

  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t freeBytes = heap_caps_get_free_size(caps);
  const size_t largest = heap_caps_get_largest_free_block(caps);
  const level_t previous = s_Level.load(std::memory_order_relaxed);
  const level_t level = s_Governor.update(freeBytes, largest);
  if (level == previous) {
    return;
  }
  s_Level.store(level, std::memory_order_release);
  ESP_LOGW(LOG_TAG, "memory pressure %s -> %s: free=%u largest=%u", levelName(previous),
           levelName(level), static_cast<unsigned>(freeBytes), static_cast<unsigned>(largest));
}

level_t current(void) {
  return s_Level.load(std::memory_order_acquire);
}

const char *levelName(level_t level) {
  switch (level) {
    case NORMAL:
      return "normal";
    case AMBER:
      return "amber";
    case RED:
      return "red";
  }
  return "unknown";
}

}  // namespace MemoryPressure
}  // namespace Furble
