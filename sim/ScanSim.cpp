#include "CameraList.h"
#include "Scan.h"

#include <utility>

#include "clock.h"
#include "driver.h"

namespace Furble {

Scan &Scan::getInstance(void) {
  static Scan instance;
  return instance;
}

void Scan::setMode(Mode) {}

void Scan::setStartProbe(std::function<void()> probe) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_StartProbe = std::move(probe);
}

bool Scan::startProbeBlocked(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_StartProbeBlocked;
}

void Scan::setTimeout(uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Deadline = Sim::clockMillis() + timeout * 1000U;
  m_HasDeadline = timeout != 0;
}

void Scan::start(std::function<void(void *)> scan_callback,
                 void *scan_result_private_data,
                 std::function<void(void *)> scan_end_callback) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  m_StartProbeBlocked = false;
  if (Sim::scenarioSettingIsTrue("scan_start_probe") && m_StartProbe) {
    // The production NimBLE call can run callbacks from another task while it
    // is starting. Run a callback-shaped probe concurrently and wait briefly
    // for it. A UI mutex held across start() makes the probe wait, which is the
    // watchdog/starvation contract this simulator is intended to catch.
    const auto probe = m_StartProbe;
    const auto completed = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([probe, completed]() {
      probe();
      completed->store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!completed->load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    m_StartProbeBlocked = !completed->load(std::memory_order_acquire);
    if (m_StartProbeBlocked) {
      worker.detach();
    } else {
      worker.join();
    }
  }

  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    generation = ++m_Generation;
    m_Active = true;
    m_EndCallbackCount = 0;
    m_PendingEvents.clear();
    m_ScanResultCallback = std::move(scan_callback);
    m_ScanEndCallback = std::move(scan_end_callback);
    m_ScanResultPrivateData = scan_result_private_data;
    m_WorkerRunning = true;
  }

  // Keep this worker asynchronous. It publishes two matching advertisement
  // events and a completion event, but never touches CameraList or LVGL.
  m_Worker = std::thread([this, generation]() {
    std::this_thread::yield();
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_Active && generation == m_Generation) {
        m_PendingEvents.push_back({generation, false, 0});
        m_PendingEvents.push_back({generation, false, 1});
        m_PendingEvents.push_back({generation, true, 0});
        m_Active = false;
      }
      m_WorkerRunning = false;
      m_WorkerDone.notify_all();
    }
  });
}

void Scan::stop(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Active = false;
    ++m_Generation;
    m_PendingEvents.clear();
    m_ScanResultCallback = nullptr;
    m_ScanEndCallback = nullptr;
    m_ScanResultPrivateData = nullptr;
    m_WorkerDone.wait(lock, [this]() { return !m_WorkerRunning; });
  }
  if (m_Worker.joinable()) {
    m_Worker.join();
  }
}

bool Scan::isActive(void) const {
  bool expired = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    expired =
        m_Active && m_HasDeadline && Sim::clockDeadlineReached(Sim::clockMillis(), m_Deadline);
    if (!expired) {
      return m_Active;
    }
  }
  const_cast<Scan *>(this)->stop();
  return false;
}

size_t Scan::endCallbackCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_EndCallbackCount;
}

size_t Scan::currentResultId(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CurrentResultId;
}

void Scan::clear(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_PendingEvents.clear();
}

void Scan::processPendingCallbacks(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  std::deque<PendingEvent> events;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    events.swap(m_PendingEvents);
  }

  for (const auto &event : events) {
    std::function<void(void *)> callback;
    void *privateData = nullptr;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (event.generation != m_Generation) {
        continue;
      }
      callback = event.end ? m_ScanEndCallback : m_ScanResultCallback;
      privateData = m_ScanResultPrivateData;
    }
    if (event.end) {
      if (callback) {
        {
          const std::lock_guard<std::mutex> lock(m_Mutex);
          ++m_EndCallbackCount;
        }
        callback(privateData);
      }
    } else if (callback) {
      {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        m_CurrentResultId = event.resultId;
      }
      callback(privateData);
    }
  }
}

// Kept as a no-op compatibility hook. All event production now happens on the
// worker and all event consumption happens in processPendingCallbacks().
void Scan::update(void) {}

}  // namespace Furble
