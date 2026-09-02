#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>

#include <esp_timer.h>
#include <cstdio>
#if defined(FURBLE_SIM)
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>
#endif

#include "Device.h"
#if defined(FURBLE_CONSOLE)
#include "BtDebugJournal.h"
#endif
#include "Scan.h"

// log tag
const char *LOG_TAG = FURBLE_STR;

namespace Furble {

namespace {
thread_local Scan *g_CallbackOwner = nullptr;
constexpr int SCAN_START_FAILED_REASON = -1;

#if defined(FURBLE_CONSOLE)
void recordScanEvent(const char *operation,
                     const char *owner,
                     uint64_t generation,
                     bool physical,
                     bool logical,
                     bool success,
                     int reason = 0) {
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::SCAN;
  event.generation = generation;
  event.reason = reason;
  event.success = success;
  event.physical = physical;
  event.logical = logical;
  snprintf(event.operation, sizeof(event.operation), "%s", operation);
  snprintf(event.owner, sizeof(event.owner), "%s", owner);
  snprintf(event.state, sizeof(event.state), "%s", logical ? "logical-active" : "logical-idle");
  snprintf(event.result, sizeof(event.result), "%s", success ? "ok" : "failed");
  snprintf(event.reason_text, sizeof(event.reason_text), "%s",
           reason == 0 ? "none" : btGapReasonName(reason));
  BtDebugJournal::instance().record(event);
}
#endif
}  // namespace

/*
 * NimBLEScan::stop() calls ble_gap_disc_cancel and does not invoke onScanEnd.
 * Apache NimBLE documents a successful cancel as "fully aborted" and allows a
 * new discovery immediately. The host lock in ble_gap_disc_cancel serializes
 * that cancellation with handleGapEvent, so no scan callback is in flight
 * after stop returns. Keep one stable proxy and advance the logical generation
 * after each quiescent stop.
 */
class Scan::CallbackProxy final: public NimBLEScanCallbacks {
 public:
  explicit CallbackProxy(Scan *owner) : m_Owner(owner) {}

  void onResult(const NimBLEAdvertisedDevice *device) override { m_Owner->onResult(device); }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    m_Owner->onScanEnd(results, reason);
  }

 private:
  Scan *m_Owner;
};

Scan &Scan::getInstance(void) {
  static Scan instance;

  if (instance.m_Scan == nullptr) {
    instance.m_Server = NimBLEDevice::createServer();

    instance.m_Scan = NimBLEDevice::getScan();
    instance.m_Scan->setActiveScan(true);
    instance.applyMode(instance.m_Mode);
  }

  return instance;
}

Scan::~Scan() = default;

Scan::Scan() : m_CallbackProxy(std::make_unique<CallbackProxy>(this)) {}

uint64_t Scan::monotonicUs(void) {
  return static_cast<uint64_t>(esp_timer_get_time());
}

void Scan::applyMode(Mode mode) {
  const auto &duty = m_Duty[static_cast<size_t>(mode)];

  // esp-nimble-cpp takes milliseconds and converts to 0.625ms units internally
  m_Scan->setInterval(duty.interval);
  m_Scan->setWindow(duty.window);
}

void Scan::setMode(Mode mode) {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  m_Mode = mode;
}

void Scan::setTimeout(uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  m_Timeout = timeout;
}

void Scan::handleResult(uint64_t generation, const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr) {
    return;
  }

  NimBLEScanCallbacks *custom = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    if (!m_Active || generation != m_Generation
        || (m_DeadlineUs != 0 && monotonicUs() >= m_DeadlineUs)) {
      return;
    }

    if (m_CallbackMode == CallbackMode::CUSTOM) {
      custom = m_CustomCallbacks;
      if (custom != nullptr) {
        ++m_CallbacksInFlight;
      }
    } else if (m_CallbackMode == CallbackMode::DISCOVERY) {
      if (m_PendingEvents.size() >= MAX_PENDING_RESULTS) {
        ++m_DroppedResults;
        ESP_LOGW(LOG_TAG, "Dropping scan advertisement, queue full (%u)",
                 static_cast<unsigned>(MAX_PENDING_RESULTS));
        return;
      }

      PendingEvent event;
      event.type = PendingEvent::Type::RESULT;
      event.generation = generation;
      event.device = *pDevice;
      event.callback = m_ScanResultCallback;
      event.privateData = m_ScanResultPrivateData;
      m_PendingEvents.push_back(std::move(event));
      return;
    }
  }

  // Custom reconnect/pairing callbacks are deliberately outside the state
  // mutex. stop() waits for this in-flight count before the owner can destroy
  // its callback object, and the generation check above rejects old proxies.
  if (custom != nullptr) {
    Scan *previousOwner = g_CallbackOwner;
    g_CallbackOwner = this;
    custom->onResult(pDevice);
    g_CallbackOwner = previousOwner;
    {
      const std::lock_guard<std::mutex> lock(m_StateMutex);
      if (--m_CallbacksInFlight == 0) {
        m_CallbackIdle.notify_all();
      }
    }
  }
}

void Scan::handleScanEnd(uint64_t generation, const NimBLEScanResults &results, int reason) {
  ESP_LOGI(LOG_TAG, "Scan ended, reason %d", reason);

  NimBLEScanCallbacks *custom = nullptr;
#if defined(FURBLE_CONSOLE)
  const char *owner = "scan";
#endif
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    if (!m_Active || generation != m_Generation) {
      return;
    }

    m_Active = false;
    m_DeadlineUs = 0;
    if (m_CallbackMode == CallbackMode::CUSTOM) {
#if defined(FURBLE_CONSOLE)
      owner = "custom";
#endif
      custom = m_CustomCallbacks;
      if (custom != nullptr) {
        ++m_CallbacksInFlight;
      }
      m_CustomCallbacks = nullptr;
      m_CallbackMode = CallbackMode::IDLE;
    } else if (m_CallbackMode == CallbackMode::DISCOVERY) {
#if defined(FURBLE_CONSOLE)
      owner = "discovery";
#endif
      PendingEvent event;
      event.type = PendingEvent::Type::END;
      event.generation = generation;
      event.callback = m_ScanEndCallback;
      event.privateData = m_ScanResultPrivateData;
      m_PendingEvents.push_back(std::move(event));
      m_ScanResultCallback = nullptr;
      m_ScanEndCallback = nullptr;
      m_ScanResultPrivateData = nullptr;
      m_CallbackMode = CallbackMode::IDLE;
    }
  }

#if defined(FURBLE_CONSOLE)
  recordScanEvent("end", owner, generation, reason != SCAN_START_FAILED_REASON, false, reason == 0,
                  reason);
#endif

  if (custom != nullptr) {
    Scan *previousOwner = g_CallbackOwner;
    g_CallbackOwner = this;
    custom->onScanEnd(results, reason);
    g_CallbackOwner = previousOwner;
    {
      const std::lock_guard<std::mutex> lock(m_StateMutex);
      if (--m_CallbacksInFlight == 0) {
        m_CallbackIdle.notify_all();
      }
    }
  }
}

/** BLE Advertisement callback entry point for compatibility and tests. */
void Scan::onResult(const NimBLEAdvertisedDevice *pDevice) {
  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    generation = m_Generation;
  }
  handleResult(generation, pDevice);
}

/** BLE scan end callback entry point for compatibility and tests. */
void Scan::onScanEnd(const NimBLEScanResults &results, int reason) {
  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    generation = m_Generation;
  }
  handleScanEnd(generation, results, reason);
}

bool Scan::start(std::function<void(void *)> scanCallback,
                 void *scanPrivateData,
                 std::function<void(void *)> scanEndCallback) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  m_Server->start();

  Mode mode;
  uint32_t timeout;
  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    ++m_Generation;
    generation = m_Generation;
    m_CallbackMode = CallbackMode::DISCOVERY;
    m_CustomCallbacks = nullptr;
    m_ScanResultCallback = std::move(scanCallback);
    m_ScanEndCallback = std::move(scanEndCallback);
    m_ScanResultPrivateData = scanPrivateData;
    m_PendingEvents.clear();
    m_DroppedResults = 0;
    m_Active = true;
    timeout = m_Timeout;
    mode = m_Mode;
    m_DeadlineUs = timeout == 0 ? 0 : monotonicUs() + (timeout * 1000000ULL);
  }

#if defined(FURBLE_SIM)
  runStartProbe();
#endif

  bool physical = false;
  if (m_Scan != nullptr) {
    m_Scan->setScanCallbacks(m_CallbackProxy.get());
    applyMode(mode);
    physical = m_Scan->start(timeout * 1000, false);
  }
#if defined(FURBLE_CONSOLE)
  recordScanEvent("start", "discovery", generation, physical, physical, physical,
                  physical ? 0 : SCAN_START_FAILED_REASON);
#endif
  if (!physical) {
    const NimBLEScanResults results;
    handleScanEnd(generation, results, SCAN_START_FAILED_REASON);
  }
  return physical;
}

bool Scan::start(NimBLEScanCallbacks *pScanCallbacks, uint32_t duration, bool wantDuplicates) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);

  uint64_t generation;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    ++m_Generation;
    generation = m_Generation;
    m_CallbackMode = CallbackMode::CUSTOM;
    m_CustomCallbacks = pScanCallbacks;
    m_ScanResultCallback = nullptr;
    m_ScanEndCallback = nullptr;
    m_ScanResultPrivateData = nullptr;
    m_PendingEvents.clear();
    m_DroppedResults = 0;
    m_Active = true;
    m_DeadlineUs = duration == 0 ? 0 : monotonicUs() + (duration * 1000ULL);
  }

  // Pairing and reconnect ignore the user preset, a low duty cycle here turns
  // a fast reconnect into a timeout.
  bool physical = false;
  if (m_Scan != nullptr) {
    applyMode(Mode::FULL);
    m_Scan->setScanCallbacks(m_CallbackProxy.get(), wantDuplicates);
    physical = m_Scan->start(duration, false);
  }
#if defined(FURBLE_CONSOLE)
  recordScanEvent("start", "custom", generation, physical, physical, physical,
                  physical ? 0 : SCAN_START_FAILED_REASON);
#endif
  if (!physical) {
    const NimBLEScanResults results;
    handleScanEnd(generation, results, SCAN_START_FAILED_REASON);
  }
  return physical;
}

void Scan::stop(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
#if defined(FURBLE_CONSOLE)
  bool wasActive = false;
  uint64_t generation = 0;
#endif
  {
    std::unique_lock<std::mutex> lock(m_StateMutex);
#if defined(FURBLE_CONSOLE)
    wasActive = m_Active;
    generation = m_Generation;
#endif
    m_Active = false;
    m_DeadlineUs = 0;
    ++m_Generation;
    m_CallbackMode = CallbackMode::IDLE;
    m_CustomCallbacks = nullptr;
    m_ScanEndCallback = nullptr;
    m_ScanResultPrivateData = nullptr;
    m_ScanResultCallback = nullptr;
    m_PendingEvents.clear();
    // A custom callback may request cancellation from its own callback. It is
    // already guaranteed to be alive until that call returns, so waiting here
    // would self-deadlock. Other callers still wait for the callback to leave
    // before their owner can be destroyed.
    if (g_CallbackOwner != this) {
      m_CallbackIdle.wait(lock, [this]() { return m_CallbacksInFlight == 0; });
    }
  }

  // State is invalidated before stop. NimBLEScan::stop() does not invoke
  // onScanEnd, and its cancellation barrier leaves no host callback in flight.
  if (m_Scan != nullptr) {
    m_Scan->stop();
  }
#if defined(FURBLE_CONSOLE)
  if (wasActive) {
    recordScanEvent("stop", "scan", generation, false, false, true);
  }
#endif
}

void Scan::expire(void) {
  stop();
}

bool Scan::isActive(void) const {
  bool expired = false;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    expired = m_Active && m_DeadlineUs != 0 && monotonicUs() >= m_DeadlineUs;
    if (!expired) {
      return m_Active;
    }
  }
  const_cast<Scan *>(this)->expire();
  return false;
}

void Scan::clear(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  if (m_Scan != nullptr) {
    m_Scan->clearResults();
  }
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  m_PendingEvents.clear();
  m_DroppedResults = 0;
}

void Scan::processPendingCallbacks(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  std::deque<PendingEvent> events;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    events.swap(m_PendingEvents);
  }

  for (auto &event : events) {
    // The lifecycle lock prevents a concurrent start/stop from changing the
    // generation between this check and the callback. Do not hold the state
    // mutex while matching or updating the UI: those operations are allowed
    // to be expensive and may re-enter Scan.
    {
      const std::lock_guard<std::mutex> lock(m_StateMutex);
      if (event.generation != m_Generation) {
        continue;
      }
    }
    if (event.type == PendingEvent::Type::RESULT) {
      if (CameraList::match(&event.device) && event.callback != nullptr) {
        ESP_LOGI(LOG_TAG, "RSSI(%s) = %d", event.device.getName().c_str(), event.device.getRSSI());
        event.callback(event.privateData);
      }
    } else if (event.callback != nullptr) {
#if defined(FURBLE_SIM)
      {
        const std::lock_guard<std::mutex> lock(m_StateMutex);
        ++m_EndCallbackCount;
      }
#endif
      event.callback(event.privateData);
    }
  }
}

size_t Scan::droppedResultCount(void) const {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  return m_DroppedResults;
}

#if defined(FURBLE_SIM)
void Scan::setStartProbe(std::function<void()> probe) {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  m_StartProbe = std::move(probe);
}

bool Scan::startProbeBlocked(void) const {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  return m_StartProbeBlocked;
}

size_t Scan::endCallbackCount(void) const {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  return m_EndCallbackCount;
}

void Scan::runStartProbe(void) {
  // Only runs when a scenario installed a probe with setStartProbe(), which
  // the UI does only under "seed scan_start_probe true". Without a probe this
  // returns before touching a thread or the wall clock.
  std::function<void()> probe;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    m_StartProbeBlocked = false;
    probe = m_StartProbe;
  }
  if (!probe) {
    return;
  }

  // A callback-shaped probe running concurrently with the start. If it has to
  // wait for the UI lock the start path is holding a lock a NimBLE callback
  // could need, which is the watchdog-sensitive starvation this guards.
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

  const bool blocked = !completed->load(std::memory_order_acquire);
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    m_StartProbeBlocked = blocked;
    if (blocked) {
      m_ProbeWorkers.push_back(std::move(worker));
    }
  }
  if (!blocked) {
    worker.join();
  }
}

void Scan::joinStartProbes(void) {
  std::vector<std::thread> workers;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    workers.swap(m_ProbeWorkers);
  }
  for (auto &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}
#endif

}  // namespace Furble
