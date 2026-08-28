// Regression for the saved Fujifilm Secure reconnect scan match.
//
// A paired X100VI in reconnect standby advertises either the pairing service
// or only the Secure service, depending on its session state (hardware trace
// 2026-08-28). The saved-scan matcher must accept both forms; the serial
// compare remains the identity check. Before the fix a Secure-service-only
// advertisement was rejected silently and the 60 s scan window timed out.
//
// Each scenario pairs, disconnects furble-side, then reconnects with the
// advertisement arriving about one second into the SAVED scan. The connect
// must complete promptly instead of running out the scan window.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "Device.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "Scan.h"

namespace Furble {
// Scan is the production implementation. This seam only supplies the list
// matcher because this test is about the saved-camera advertisement match,
// not discovery-list dispatch (covered by advertisement_dispatch_test).
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  return false;
}
}  // namespace Furble

namespace {

using host_clock = std::chrono::steady_clock;

bool runScenario(const char *label, const std::vector<NimBLEUUID> &services, uint64_t address) {
  using namespace Furble;

  Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.address = NimBLEAddress(address, 0);
  config.advertised_services = services;
  Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEAdvertisedDevice advertisement = peer.advertisement();
  FujifilmSecure camera(&advertisement);

  // First connect: pairing path, no scan. Marks the camera paired.
  if (!camera.connect(ESP_PWR_LVL_P3, 1000)) {
    std::cerr << "FAIL(" << label << "): initial pairing connect failed\n";
    return false;
  }

  // Furble-initiated disconnect, as on the bench.
  camera.disconnect();

  // Second connect: paired, so the SAVED scan-first path runs.
  bool reconnected = false;
  const auto start = host_clock::now();
  std::thread connector([&] { reconnected = camera.connect(ESP_PWR_LVL_P3, 5000); });

  // The advertisement arrives about one second after "Scanning".
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  NimBLEDevice::getScan()->emitResult(&advertisement);

  connector.join();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(host_clock::now() - start).count();

  if (!reconnected) {
    std::cerr << "FAIL(" << label << "): saved reconnect did not complete (elapsed " << elapsed
              << " ms)\n";
    return false;
  }
  if (elapsed > 4000) {
    std::cerr << "FAIL(" << label << "): saved reconnect took " << elapsed << " ms\n";
    return false;
  }
  std::cout << label << ": PASS (" << elapsed << " ms)\n";
  return true;
}

}  // namespace

int main() {
  // Hard watchdog: a hung scan wait must fail, not hang the harness.
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    std::cerr << "FAIL: watchdog timeout, saved reconnect scan never resolved\n";
    std::_Exit(2);
  }).detach();

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  // Pairing service, as advertised right after a pairing session.
  const NimBLEUUID pairService {0x123d8f06, 0x62a1, 0x4935, 0x9322833c531ee225};
  // Secure service only, the hardware-observed reconnect standby variant.
  const NimBLEUUID secureService {0xa9d2b304, 0xe8d6, 0x4902, 0x8336352b772d7597};

  bool pass = true;
  pass =
      runScenario("saved scan match (pairing service)", {pairService}, 0x112233445566ULL) && pass;
  pass = runScenario("saved scan match (Secure service only)", {secureService}, 0x112233445577ULL)
         && pass;

  return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
