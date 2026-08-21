// KNOWN-FAILING regression test (registered WILL_FAIL in CMakeLists).
//
// Finding: FujifilmBasic::_connect() reports a successful connect when the
// shutter characteristic is missing, so furble shows a connected camera whose
// shutter and focus do nothing, with no error surfaced.
//
// When the shutter service is present but the shutter characteristic is absent,
// getCharacteristic(CHR_SHUTTER_UUID) returns nullptr. FujifilmBasic.cpp only
// logs it and falls through to `return true`, leaving m_Shutter null. Later
// shutter and focus commands short-circuit in sendShutterCommand() because
// m_Shutter is null, so nothing is sent. The camera looks connected and active
// while every command is silently dropped.
//
// FujifilmSecure.cpp handles the same case correctly: it `return false` when the
// shutter characteristic is null, so the connect fails visibly instead of
// stranding the user on a dead remote. FujifilmBasic should match.
//
// Correct contract: a camera that reports connected must be able to fire the
// shutter, or the connect must fail. This test asserts that contract. Today the
// connect returns true and zero shutter writes reach the peer, so the assertion
// fails and CI stays green through the WILL_FAIL marker. When FujifilmBasic
// gains the missing `return false`, connect() returns false, the contract holds,
// the test passes, and the WILL_FAIL marker flips the job red.

#include <cstdint>
#include <iostream>
#include <memory>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

bool testMissingShutterCharacteristicIsNotSilent() {
  std::cout << "test: a connected FujifilmBasic must be able to fire the shutter\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  peer.suppressCharacteristic(Furble::Host::FujifilmVirtualCamera::shutterServiceUUID(),
                              Furble::Host::FujifilmVirtualCamera::shutterCharacteristicUUID());
  auto camera = makeCamera(peer);

  const bool connected = camera->connect(ESP_PWR_LVL_P3, 1000);

  peer.clearEvents();
  if (connected) {
    camera->shutterPress();
    camera->shutterRelease();
  }
  bool shutterFired = false;
  for (const auto &write : peer.writes()) {
    if (write.characteristic
        == Furble::Host::FujifilmVirtualCamera::shutterCharacteristicUUID().toString()) {
      shutterFired = true;
      break;
    }
  }

  // The contract holds if the connect failed visibly, or if it reports connected
  // and the shutter actually fires. It is violated when the connect reports
  // success but the shutter is silently dead, which is the current behavior.
  check(!connected || shutterFired, "a connect that reports success must drive a working shutter");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testMissingShutterCharacteristicIsNotSilent();

  if (g_Failures != 0) {
    std::cout << "missing shutter characteristic harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "missing shutter characteristic harness: PASS\n";
  return 0;
}
