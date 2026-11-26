// Regression for the Fujifilm Secure saved-reconnect address hand-off.
//
// A Fujifilm Secure body advertises a resolvable private address and re-pairs
// under a new one, while the saved record keeps the address it had when it was
// stored. The serial-keyed rescan finds the camera anyway, but the saved
// connect then handed the stored address to NimBLEClient::connect(), which is a
// peer that is no longer there. The match is the only place the live address is
// known, so it has to be carried from the advertisement to the connect and used
// for the link.
//
// The test stores the record through the camera's own serialisation, as
// FujifilmSecure's NVS constructor reads it, then lets the peer advertise at a
// new address. Red-first evidence: with the stored address left in place the
// virtual peer refuses the link (acceptConnection compares the address), the
// reconnect fails, and the update this test asserts never happens.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
// matcher because this test is about the saved-camera address hand-off, not
// discovery-list dispatch (covered by advertisement_dispatch_test).
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  return false;
}
}  // namespace Furble

namespace {

using host_clock = std::chrono::steady_clock;
using namespace Furble;

constexpr uint64_t STORED_ADDRESS = 0x112233445566ULL;
constexpr uint64_t LIVE_ADDRESS = 0xa1b2c3d4e5f6ULL;

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

/** Build the NVS record a previous pairing would have stored. */
std::vector<uint8_t> storedRecord(const NimBLEAdvertisedDevice &advertisement) {
  FujifilmSecure scanned(&advertisement);
  std::vector<uint8_t> blob(scanned.getSerialisedBytes(), 0);
  if (!scanned.serialise(blob.data(), blob.size())) {
    std::cerr << "  FAIL: the Secure record did not serialise\n";
    g_Failures++;
    return {};
  }
  return blob;
}

int run(void) {
  NimBLEDevice::resetMock();
  Device::init(TxPower::P3);

  Host::FujifilmVirtualCamera::Config storedConfig;
  storedConfig.secure = true;
  storedConfig.address = NimBLEAddress(STORED_ADDRESS, 0);
  Host::FujifilmVirtualCamera storedPeer(storedConfig);
  const NimBLEAdvertisedDevice storedAdvertisement = storedPeer.advertisement();

  // The live camera: same body, same serial, a rotated address in reconnect
  // standby, where it advertises the pairing service only.
  Host::FujifilmVirtualCamera::Config liveConfig = storedConfig;
  liveConfig.address = NimBLEAddress(LIVE_ADDRESS, 0);
  liveConfig.advertised_services = {
      NimBLEUUID {0x123d8f06, 0x62a1, 0x4935, 0x9322833c531ee225}
  };
  Host::FujifilmVirtualCamera livePeer(liveConfig);
  NimBLEDevice::setMockPeer(&livePeer);

  const std::vector<uint8_t> blob = storedRecord(storedAdvertisement);
  if (blob.empty()) {
    return EXIT_FAILURE;
  }
  const NimBLEAdvertisedDevice liveAdvertisement = livePeer.advertisement();

  FujifilmSecure saved(blob.data(), blob.size());
  if (!check(saved.getAddress() == NimBLEAddress(STORED_ADDRESS, 0),
             "the saved record starts on its stored address")) {
    return EXIT_FAILURE;
  }

  bool connected = false;
  const auto start = host_clock::now();
  std::thread connector([&] { connected = saved.connect(TxPower::P3, 5000); });

  // The advertisement arrives shortly after the reconnect scan starts.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  NimBLEDevice::getScan()->emitResult(&liveAdvertisement);

  connector.join();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(host_clock::now() - start).count();
  if (!connected) {
    std::cerr << "  FAIL: the saved reconnect did not complete (elapsed " << elapsed << " ms)\n";
    g_Failures++;
  }

  check(saved.getAddress() == NimBLEAddress(LIVE_ADDRESS, 0),
        "the reconnect adopts the address the camera advertised");

  // The updated address is what the record carries, so the next save writes it
  // and the console, the companion service and CameraList::remove all see the
  // live peer instead of the one the camera left behind.
  std::vector<uint8_t> resaved(saved.getSerialisedBytes(), 0);
  if (check(saved.serialise(resaved.data(), resaved.size()), "the camera re-serialises")) {
    // FujifilmSecure::nvs_t is the name first, then the address, then the type
    // and the serial. Read the address field at that fixed offset.
    uint64_t address = 0;
    std::memcpy(&address, resaved.data() + MAX_NAME, sizeof(address));
    check(address == LIVE_ADDRESS, "the serialised record carries the live address");
  }

  saved.disconnect();

  return (g_Failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main() {
  // Hard watchdog: a hung scan wait must fail, not hang the harness.
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    std::cerr << "FAIL: watchdog timeout, saved reconnect never resolved\n";
    std::_Exit(2);
  }).detach();

  const int result = run();
  if (result == EXIT_SUCCESS) {
    std::cout << "fujifilm-secure-address: PASS\n";
  }
  return result;
}
