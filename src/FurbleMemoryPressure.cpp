#include "FurbleMemoryPressure.h"

#include <atomic>
#include <mutex>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace Furble {
namespace MemoryPressure {

namespace {

constexpr const char *LOG_TAG = "memory";
constexpr int64_t TICK_PERIOD_US = 1000000;
constexpr uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// The UI loop steps the governor and any connecting task may escalate it, so
// both take s_Mutex. It is never held across a delay or another lock.
std::mutex s_Mutex;
Governor s_Governor;
std::atomic<level_t> s_Level {NORMAL};
std::atomic<size_t> s_MQTTStartExtra {0};
std::atomic<escalate_hook_t> s_EscalateHook {nullptr};
int64_t s_LastTickUs = 0;
bool s_Ticked = false;

void logChange(level_t previous, level_t level, size_t freeBytes, size_t largest) {
  if (level == previous) {
    return;
  }
  ESP_LOGW(LOG_TAG, "memory pressure %s -> %s: free=%u largest=%u", levelName(previous),
           levelName(level), static_cast<unsigned>(freeBytes), static_cast<unsigned>(largest));
}

}  // namespace

void tick(void) {
  level_t previous = NORMAL;
  level_t level = NORMAL;
  size_t freeBytes = 0;
  size_t largest = 0;
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    const int64_t now = esp_timer_get_time();
    if (s_Ticked && ((now - s_LastTickUs) < TICK_PERIOD_US)) {
      return;
    }
    s_Ticked = true;
    s_LastTickUs = now;

    freeBytes = heap_caps_get_free_size(CAPS);
    largest = heap_caps_get_largest_free_block(CAPS);
    previous = s_Level.load(std::memory_order_relaxed);
    level = s_Governor.update(freeBytes, largest, s_MQTTStartExtra.load(std::memory_order_relaxed));
    s_Level.store(level, std::memory_order_release);
  }
  logChange(previous, level, freeBytes, largest);
}

void admit(size_t bytes) {
  level_t previous = NORMAL;
  level_t level = NORMAL;
  size_t freeBytes = 0;
  size_t largest = 0;
  {
    const std::lock_guard<std::mutex> lock(s_Mutex);
    freeBytes = heap_caps_get_free_size(CAPS);
    largest = heap_caps_get_largest_free_block(CAPS);
    previous = s_Level.load(std::memory_order_relaxed);
    level = s_Governor.escalate(classifyAfter(freeBytes, largest, bytes));
    s_Level.store(level, std::memory_order_release);
  }
  if (level == previous) {
    return;
  }
  ESP_LOGW(LOG_TAG, "memory pressure %s -> %s before a %u-byte allocation: free=%u largest=%u",
           levelName(previous), levelName(level), static_cast<unsigned>(bytes),
           static_cast<unsigned>(freeBytes), static_cast<unsigned>(largest));
  // Outside s_Mutex: the hook takes the WebUI's own lock to find its task.
  const escalate_hook_t hook = s_EscalateHook.load(std::memory_order_acquire);
  if (hook != nullptr) {
    hook();
  }
}

void setEscalateHook(escalate_hook_t hook) {
  s_EscalateHook.store(hook, std::memory_order_release);
}

void setMQTTStartExtra(size_t bytes) {
  s_MQTTStartExtra.store(bytes, std::memory_order_relaxed);
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
