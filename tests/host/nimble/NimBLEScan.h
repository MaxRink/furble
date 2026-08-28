#ifndef FURBLE_HOST_NIMBLE_SCAN_H
#define FURBLE_HOST_NIMBLE_SCAN_H

#include <cstdint>

class NimBLEAdvertisedDevice;
bool nimbleMockGapScanStartAllowed(void);
void nimbleMockSetGapScanStartAllowed(bool allowed);
// Absent-peer model: false when the advertisement's address is flagged absent
// via NimBLEDevice::setScanAbsentAddress(), so the scan never delivers it even
// while an advertiser keeps emitting it. See MockNimBLE.h.
bool nimbleMockScanDeliveryAllowed(const NimBLEAdvertisedDevice *device);
class NimBLEScanResults {};
class NimBLEServer {
 public:
  void start() {}
};

class NimBLEScanCallbacks {
 public:
  virtual ~NimBLEScanCallbacks() = default;
  virtual void onResult(const NimBLEAdvertisedDevice *) {}
  virtual void onScanEnd(const NimBLEScanResults &, int) {}
};

class NimBLEScan {
 public:
  void setActiveScan(bool) {}
  void setInterval(uint16_t) {}
  void setWindow(uint16_t) {}
  void setScanCallbacks(NimBLEScanCallbacks *callbacks, bool = false) { m_Callbacks = callbacks; }
  bool start(uint32_t, bool = false) {
    m_Scanning = nimbleMockGapScanStartAllowed();
    return m_Scanning;
  }
  void stop() { m_Scanning = false; }
  void clearResults() {}
  bool isScanning() const { return m_Scanning; }
  NimBLEScanCallbacks *callbacks() const { return m_Callbacks; }
  void emitResult(const NimBLEAdvertisedDevice *device) {
    if ((m_Callbacks != nullptr) && nimbleMockScanDeliveryAllowed(device)) {
      m_Callbacks->onResult(device);
    }
  }
  void emitEnd(int reason = 0) {
    if (m_Callbacks != nullptr) {
      NimBLEScanResults results;
      m_Callbacks->onScanEnd(results, reason);
    }
  }

 private:
  NimBLEScanCallbacks *m_Callbacks = nullptr;
  bool m_Scanning = false;
};

#endif
