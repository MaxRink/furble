#include "TestSyncController.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

#include "FurbleTestSync.h"

namespace {

struct PointState {
  bool barrierArmed = false;
  uint32_t barrierTimeoutMs = 0;
  bool parked = false;
  bool released = false;
  std::function<void(void)> callback;
};

std::mutex g_Mutex;
std::condition_variable g_Cond;
std::map<std::string, PointState> g_Points;
bool g_TimedOut = false;

}  // namespace

namespace Furble {
namespace TestSync {

void point(const char *name) {
  std::function<void(void)> callback;

  {
    std::unique_lock<std::mutex> lock(g_Mutex);
    auto it = g_Points.find(name);
    if (it != g_Points.end()) {
      PointState &state = it->second;
      callback = state.callback;

      if (state.barrierArmed) {
        state.parked = true;
        state.released = false;
        g_Cond.notify_all();

        const bool released =
            g_Cond.wait_for(lock, std::chrono::milliseconds(state.barrierTimeoutMs),
                            [&state] { return state.released; });
        if (!released) {
          g_TimedOut = true;
        }

        state.parked = false;
        state.barrierArmed = false;
        g_Cond.notify_all();
      }
    }
  }

  if (callback) {
    callback();
  }
}

void reset(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  g_Points.clear();
  g_TimedOut = false;
}

void armBarrier(const char *name, uint32_t timeout_ms) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  PointState &state = g_Points[name];
  state.barrierArmed = true;
  state.barrierTimeoutMs = timeout_ms;
  state.parked = false;
  state.released = false;
}

void onPoint(const char *name, std::function<void(void)> callback) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  g_Points[name].callback = std::move(callback);
}

bool awaitArrival(const char *name, uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(g_Mutex);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  return g_Cond.wait_until(lock, deadline, [name] {
    auto it = g_Points.find(name);
    return (it != g_Points.end()) && it->second.parked;
  });
}

void release(const char *name) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  auto it = g_Points.find(name);
  if (it != g_Points.end()) {
    it->second.released = true;
    g_Cond.notify_all();
  }
}

bool anyTimedOut(void) {
  const std::lock_guard<std::mutex> lock(g_Mutex);
  return g_TimedOut;
}

}  // namespace TestSync
}  // namespace Furble
