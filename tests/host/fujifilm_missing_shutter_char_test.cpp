// Regression guard: FujifilmBasic::_connect() must not report a successful
// connect when the shutter characteristic is missing.
//
// The original bug: when the shutter service was present but the shutter
// characteristic absent, getCharacteristic(CHR_SHUTTER_UUID) returned nullptr,
// which was only logged before `return true`, leaving m_Shutter null. Shutter
// and focus commands then short-circuited on the null m_Shutter, so the camera
// looked connected and active while every command was silently dropped.
// FujifilmBasic::_connect now `return false` when the shutter characteristic is
// null, matching FujifilmSecure.
//
// Correct contract: a camera that reports connected must be able to fire the
// shutter, or the connect must fail. This test asserts that contract. With the
// guard connect() returns false, the contract holds, and the test passes as a
// normal test.

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
