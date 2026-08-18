#include <NimBLEScan.h>

#include "Device.h"
#include "Scan.h"

// log tag
const char *LOG_TAG = FURBLE_STR;

namespace Furble {

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

void Scan::applyMode(Mode mode) {
  const auto &duty = m_Duty[static_cast<size_t>(mode)];

  // esp-nimble-cpp takes milliseconds and converts to 0.625ms units internally
  m_Scan->setInterval(duty.interval);
  m_Scan->setWindow(duty.window);
}

void Scan::setMode(Mode mode) {
  m_Mode = mode;
}

void Scan::setTimeout(uint32_t timeout) {
  m_Timeout = timeout;
}

/**
 * BLE Advertisement callback.
 */
void Scan::onResult(const NimBLEAdvertisedDevice *pDevice) {
  if (CameraList::match(pDevice)) {
    ESP_LOGI(LOG_TAG, "RSSI(%s) = %d", pDevice->getName().c_str(), pDevice->getRSSI());
    if (m_ScanResultCallback != nullptr) {
      (m_ScanResultCallback)(m_ScanResultPrivateData);
    }
  }
};

/**
 * BLE scan end callback.
 */
void Scan::onScanEnd(const NimBLEScanResults &, int reason) {
  ESP_LOGI(LOG_TAG, "Scan ended, reason %d", reason);
  if (m_ScanEndCallback != nullptr) {
    (m_ScanEndCallback)(m_ScanResultPrivateData);
  }
};

void Scan::start(std::function<void(void *)> scanCallback,
                 void *scanPrivateData,
                 std::function<void(void *)> scanEndCallback) {
  m_Server->start();
  m_Scan->setScanCallbacks(this);

  m_ScanResultCallback = scanCallback;
  m_ScanEndCallback = scanEndCallback;
  m_ScanResultPrivateData = scanPrivateData;

  // apply the preset here so a settings change takes effect without a restart
  applyMode(m_Mode);
  m_Scan->start(m_Timeout * 1000, false);
}

void Scan::start(NimBLEScanCallbacks *pScanCallbacks, uint32_t duration, bool wantDuplicates) {
  // pairing and reconnect ignore the user preset, a low duty cycle here turns a
  // fast reconnect into a timeout
  applyMode(Mode::FULL);

  m_Scan->setScanCallbacks(pScanCallbacks, wantDuplicates);
  m_Scan->start(duration, false);
}

void Scan::stop(void) {
  // clear first, NimBLEScan::stop() invokes onScanEnd()
  m_ScanEndCallback = nullptr;
  m_Scan->stop();
  m_ScanResultPrivateData = nullptr;
  m_ScanResultCallback = nullptr;
}

bool Scan::isActive(void) const {
  return m_Scan->isScanning();
}

void Scan::clear(void) {
  m_Scan->clearResults();
}
}  // namespace Furble
