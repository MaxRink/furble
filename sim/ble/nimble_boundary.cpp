#include "nimble_boundary.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {

uint64_t g_NowMs = 0;
uint64_t g_NextSequence = 1;
std::mutex g_Mutex;
std::vector<ble_npl_event *> g_Events;
std::vector<ble_npl_callout *> g_Callouts;

bool due(const ble_npl_event *event, uint64_t now) {
  return event->due_ms <= now;
}

bool calloutDue(const ble_npl_callout *callout, uint32_t now) {
  return static_cast<int32_t>(now - callout->due_ticks) >= 0;
}

template <typename T>
void eraseQueued(std::vector<T *> &queue, T *target) {
  queue.erase(std::remove(queue.begin(), queue.end(), target), queue.end());
}

}  // namespace

extern "C" void furble_ble_npl_reset(void) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  for (ble_npl_event *event : g_Events) {
    event->queued = 0;
  }
  for (ble_npl_callout *callout : g_Callouts) {
    callout->queued = 0;
  }
  g_Events.clear();
  g_Callouts.clear();
  g_NowMs = 0;
  g_NextSequence = 1;
}

extern "C" uint64_t furble_ble_npl_now(void) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  return g_NowMs;
}

extern "C" void furble_ble_npl_advance(uint64_t milliseconds) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  g_NowMs += milliseconds;
}

extern "C" int furble_ble_npl_eventq_put(ble_npl_event *event, uint64_t delay_ms) {
  if (event == nullptr || event->fn == nullptr) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (event->queued != 0) {
    return -1;
  }
  event->due_ms = g_NowMs + delay_ms;
  event->sequence = g_NextSequence++;
  event->queued = 1;
  g_Events.push_back(event);
  return 0;
}

extern "C" int furble_ble_npl_eventq_remove(ble_npl_event *event) {
  if (event == nullptr) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (event->queued == 0) {
    return -1;
  }
  eraseQueued(g_Events, event);
  event->queued = 0;
  return 0;
}

extern "C" int furble_ble_npl_callout_reset(ble_npl_callout *callout, ble_npl_time_t delay_ticks) {
  if (callout == nullptr || callout->fn == nullptr) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (callout->queued != 0) {
    eraseQueued(g_Callouts, callout);
  }
  callout->due_ticks = static_cast<ble_npl_time_t>(g_NowMs + delay_ticks);
  callout->sequence = g_NextSequence++;
  callout->queued = 1;
  g_Callouts.push_back(callout);
  return 0;
}

extern "C" int furble_ble_npl_callout_stop(ble_npl_callout *callout) {
  if (callout == nullptr) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (callout->queued == 0) {
    return -1;
  }
  eraseQueued(g_Callouts, callout);
  callout->queued = 0;
  return 0;
}

extern "C" uint32_t furble_ble_npl_run_due(void) {
  uint32_t dispatched = 0;
  for (;;) {
    ble_npl_event *event = nullptr;
    ble_npl_callout *callout = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_Mutex);
      const auto eventIt =
          std::min_element(g_Events.begin(), g_Events.end(),
                           [](const ble_npl_event *left, const ble_npl_event *right) {
                             return left->sequence < right->sequence;
                           });
      if (eventIt != g_Events.end() && due(*eventIt, g_NowMs)) {
        event = *eventIt;
        g_Events.erase(eventIt);
        event->queued = 0;
      } else {
        const uint32_t now = static_cast<uint32_t>(g_NowMs);
        const auto calloutIt =
            std::min_element(g_Callouts.begin(), g_Callouts.end(),
                             [](const ble_npl_callout *left, const ble_npl_callout *right) {
                               return left->sequence < right->sequence;
                             });
        if (calloutIt != g_Callouts.end() && calloutDue(*calloutIt, now)) {
          callout = *calloutIt;
          g_Callouts.erase(calloutIt);
          callout->queued = 0;
        } else {
          break;
        }
      }
    }
    if (event != nullptr) {
      event->fn(event);
    } else {
      callout->fn(callout);
    }
    ++dispatched;
  }
  return dispatched;
}

extern "C" uint32_t furble_ble_npl_pending(void) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  return static_cast<uint32_t>(g_Events.size());
}

extern "C" uint32_t furble_ble_npl_callout_pending(void) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  return static_cast<uint32_t>(g_Callouts.size());
}
