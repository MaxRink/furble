#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>

#include <esp_timer.h>

#include "Device.h"
#include "Scan.h"

// log tag
const char *LOG_TAG = FURBLE_STR;

namespace Furble {

namespace {
thread_local Scan *g_CallbackOwner = nullptr;
}

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
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    if (!m_Active || generation != m_Generation) {
      return;
    }

    m_Active = false;
    m_DeadlineUs = 0;
    if (m_CallbackMode == CallbackMode::CUSTOM) {
      custom = m_CustomCallbacks;
      if (custom != nullptr) {
        ++m_CallbacksInFlight;
      }
      m_CustomCallbacks = nullptr;
      m_CallbackMode = CallbackMode::IDLE;
    } else if (m_CallbackMode == CallbackMode::DISCOVERY) {
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

void Scan::start(std::function<void(void *)> scanCallback,
                 void *scanPrivateData,
                 std::function<void(void *)> scanEndCallback) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  m_Server->start();

  Mode mode;
  uint32_t timeout;
  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    ++m_Generation;
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

  m_Scan->setScanCallbacks(m_CallbackProxy.get());
  applyMode(mode);
  m_Scan->start(timeout * 1000, false);
}

void Scan::start(NimBLEScanCallbacks *pScanCallbacks, uint32_t duration, bool wantDuplicates) {
  stop();
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);

  {
    const std::lock_guard<std::mutex> lock(m_StateMutex);
    ++m_Generation;
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
  applyMode(Mode::FULL);
  m_Scan->setScanCallbacks(m_CallbackProxy.get(), wantDuplicates);
  m_Scan->start(duration, false);
}

void Scan::stop(void) {
  const std::lock_guard<std::recursive_mutex> dispatchLock(m_DispatchMutex);
  {
    std::unique_lock<std::mutex> lock(m_StateMutex);
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
      event.callback(event.privateData);
    }
  }
}

size_t Scan::droppedResultCount(void) const {
  const std::lock_guard<std::mutex> lock(m_StateMutex);
  return m_DroppedResults;
}

}  // namespace Furble
