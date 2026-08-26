#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>

#include "FujifilmSecure.h"
#include "Nikon.h"
#include "NimBLEDevice.h"
#include "Scan.h"

namespace Furble {
namespace {
void check(bool condition, const char *message, int &failures) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

template <typename CameraType>
void cancelSavedScan(CameraType &camera, int &failures) {
  auto &scan = Scan::getInstance();
  auto *nimble = NimBLEDevice::getScan();

  scan.start(&camera, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  check(!scan.isActive(), "saved-camera finite scan expires", failures);
  check(!nimble->isScanning(), "saved-camera expiry stops physical scan", failures);

  scan.start(&camera, 60000);
  std::thread callbackWorker([nimble]() {
    NimBLEAdvertisedDevice advertisement;
    for (size_t i = 0; i < 64; ++i) {
      nimble->emitResult(&advertisement);
    }
  });
  scan.stop();
  callbackWorker.join();
  check(!scan.isActive(), "saved-camera cancellation stops logical scan", failures);
  check(!nimble->isScanning(), "saved-camera cancellation stops physical scan", failures);
}
}  // namespace

// Scan is the production implementation. This seam only supplies the list
// matcher because this test is about saved-camera callback lifetime, not
// discovery-list dispatch (covered by advertisement_dispatch_test).
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  return false;
}
}  // namespace Furble

int main() {
  using namespace Furble;
  int failures = 0;
  NimBLEDevice::resetMock();

  NimBLEAdvertisedDevice fujiAdvertisement;
  const uint8_t fujiData[] = {0xd8, 0x04, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  fujiAdvertisement.setManufacturerData(fujiData, sizeof(fujiData));
  {
    FujifilmSecure fuji(&fujiAdvertisement);
    cancelSavedScan(fuji, failures);
  }
  // The stable proxy may still be the NimBLE callback object, but the saved
  // camera callback has been cleared before stop returns. A late host event
  // must therefore be harmless after the camera is destroyed.
  NimBLEDevice::getScan()->emitResult(&fujiAdvertisement);

  NimBLEAdvertisedDevice nikonAdvertisement;
  nikonAdvertisement.addServiceUUID(NikonBase::SERVICE_UUID);
  {
    Nikon nikon(&nikonAdvertisement);
    cancelSavedScan(nikon, failures);
  }
  NimBLEDevice::getScan()->emitResult(&nikonAdvertisement);

  if (failures != 0) {
    return EXIT_FAILURE;
  }
  std::cout << "saved scan cancellation: PASS\n";
  return EXIT_SUCCESS;
}
