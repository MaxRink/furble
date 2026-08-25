#ifndef FURBLE_HOST_NIMBLE_SCAN_H
#define FURBLE_HOST_NIMBLE_SCAN_H

#include <cstdint>

class NimBLEAdvertisedDevice;
class NimBLEScanResults {};
class NimBLEServer {};

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
  void setScanCallbacks(NimBLEScanCallbacks *, bool = false) {}
  void start(uint32_t, bool = false) {}
  void stop() {}
  void clearResults() {}
  bool isScanning() const { return false; }
};

#endif
