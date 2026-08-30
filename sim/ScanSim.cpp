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

Scan::~Scan() {
  shutdown();
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
  m_Timeout = timeout;
}

bool Scan::start(std::function<void(void *)> scan_callback,
                 void *scan_result_private_data,
                 std::function<void(void *)> scan_end_callback) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  m_StartProbeBlocked = false;
  if (Sim::scenarioSettingIsTrue("scan_start_fail")) {
    // A rejected physical start has no end event. Keep this seam callback-free
    // so the shared UI caller must handle the returned failure explicitly.
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Active = false;
    ++m_Generation;
    m_EndCallbackCount = 0;
    m_PendingEvents.clear();
    m_ScanResultCallback = nullptr;
    m_ScanEndCallback = nullptr;
    m_ScanResultPrivateData = nullptr;
    return false;
  }
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
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_ProbeWorkers.push_back(std::move(worker));
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
    m_HasDeadline = m_Timeout != 0;
    m_Deadline = m_HasDeadline ? Sim::clockMillis() + m_Timeout * 1000U : 0;
    m_WorkerRunning = true;
  }

  // Keep this worker asynchronous. It publishes two matching advertisement
  // events and a completion event, but never touches CameraList or LVGL.
  m_Worker = std::thread([this, generation]() {
    std::this_thread::yield();
    bool hasDeadline = false;
    uint64_t deadline = 0;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_Active && generation == m_Generation) {
        hasDeadline = m_HasDeadline;
        deadline = m_Deadline;
        m_PendingEvents.push_back({generation, false, 0});
        m_PendingEvents.push_back({generation, false, 1});
      }
    }

    // A simulated scan completes only from its worker, just as a production
    // scan completes from NimBLE's onScanEnd callback. A zero timeout means
    // scan until explicitly stopped, so both finite and infinite workers wait
    // for their owned completion condition. isActive() only observes state.
    std::unique_lock<std::mutex> lock(Sim::schedulerMutex());
    Sim::schedulerCondition().wait(lock, [this, generation, hasDeadline, deadline]() {
      return Sim::schedulerStopping() || m_Generation != generation
             || (hasDeadline
                 && Sim::clockDeadlineReached(Sim::clockMillis(),
                                               static_cast<uint32_t>(deadline)));
    });
    lock.unlock();

    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_Active && generation == m_Generation) {
        m_PendingEvents.push_back({generation, true, 0});
        m_Active = false;
      }
      m_WorkerRunning = false;
      m_WorkerDone.notify_all();
    }
  });
  return true;
}

void Scan::stop(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    ++m_StopCount;
  }
  {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Active = false;
    ++m_Generation;
    m_PendingEvents.clear();
    m_ScanResultCallback = nullptr;
    m_ScanEndCallback = nullptr;
    m_ScanResultPrivateData = nullptr;
    // Wake a finite-scan worker before waiting for it to report quiescence.
    // The worker may be blocked on the shared virtual-time condition rather
    // than on m_Mutex.
    Sim::schedulerCondition().notify_all();
    m_WorkerDone.wait(lock, [this]() { return !m_WorkerRunning; });
  }
  if (m_Worker.joinable()) {
    m_Worker.join();
  }
}

void Scan::shutdown(void) {
  stop();
  std::vector<std::thread> probes;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    probes.swap(m_ProbeWorkers);
  }
  for (auto &probe : probes) {
    if (probe.joinable()) {
      probe.join();
    }
  }
}

bool Scan::isActive(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Active;
}

size_t Scan::endCallbackCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_EndCallbackCount;
}

size_t Scan::stopCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_StopCount;
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
