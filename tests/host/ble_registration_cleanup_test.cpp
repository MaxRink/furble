// Regression for a failed Secure registration that tears down a link before
// NimBLE dispatches its asynchronous GAP disconnect event.
//
// NimBLE may report the link as down while the disconnect event is still queued.
// Deleting the client in that window loses the connection handle, producing
// "Disconnected client not found" and leaving the following scan unusable on
// hardware.  The test keeps the event pending long enough to prove that the
// failed registration cleanup leaves a live client for NimBLE to reap.

#include <iostream>

#include "Device.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "Scan.h"

namespace Furble {
namespace {
size_t g_Matches = 0;

bool check(bool condition, const char *message, int &failures) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
  return condition;
}

class RawScanCallbacks final: public NimBLEScanCallbacks {
 public:
  explicit RawScanCallbacks(size_t &results) : m_Results(results) {}

  void onResult(const NimBLEAdvertisedDevice *) override { ++m_Results; }

 private:
  size_t &m_Results;
};
}  // namespace

// Scan's discovery mode is not used here, but its production object also owns
// the custom callback path. Keep this target self-contained like
// scan_generation_test while still exercising the real Scan implementation.
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  ++g_Matches;
  return true;
}
}  // namespace Furble

int main() {
  using namespace Furble;
  int failures = 0;
  NimBLEDevice::resetMock();
  NimBLEDevice::setDeferredClientDelete(true);
  NimBLEDevice::setAsyncDisconnect(true);
  Device::init(ESP_PWR_LVL_P3);

  Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Host::FujifilmVirtualCamera peer(config);
  peer.suppressCharacteristic(Host::FujifilmVirtualCamera::shutterServiceUUID(),
                              Host::FujifilmVirtualCamera::shutterCharacteristicUUID());
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000),
        "Secure registration fails at the missing shutter characteristic", failures);
  check(NimBLEDevice::liveClientCount() == 1,
        "failed registration retains the client until the queued GAP event", failures);
  check(NimBLEDevice::completeAsyncDisconnect(),
        "the queued disconnect event still resolves to its client", failures);
  check(NimBLEDevice::asyncDisconnectEventFound(),
        "disconnect event finds the original connection handle", failures);
  check(NimBLEDevice::liveClientCount() == 0,
        "NimBLE reaps the self-deleting client after the callback", failures);

  size_t results = 0;
  RawScanCallbacks callbacks(results);
  auto &scan = Scan::getInstance();
  scan.start(&callbacks, 1000, true);
  NimBLEDevice::getScan()->emitResult(&advertisement);
  check(results == 1, "a custom scan receives an advertisement after cleanup", failures);
  scan.stop();

  NimBLEDevice::setAsyncDisconnect(false);
  NimBLEDevice::setDeferredClientDelete(false);
  NimBLEDevice::resetMock();
  if (failures == 0) {
    std::cout << "BLE registration cleanup: PASS\n";
  }
  return failures == 0 ? 0 : 1;
}
