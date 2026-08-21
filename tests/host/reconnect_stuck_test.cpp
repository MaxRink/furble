// Host regression test for the fast-reconnect "stuck connecting" bug (plan 76).
//
// On a fast disconnect then reconnect the camera still holds the CCCD
// subscriptions from the previous session. furble re-runs the registration
// handshake, and the CCCD subscribe writes are the flaky step: on a stale
// session an acknowledged CCCD write never gets its ATT write response and
// blocks the connect, or the camera rejects the re-subscribe and the handshake
// aborts. Either way the connect never completes and the UI stays "connecting"
// for 90 s and longer.
//
// The fix makes the CCCD subscribe writes unacknowledged (bounded, cannot block)
// and treats a subscribe failure as non-fatal. This test models the
// stale-session reconnect with the FujifilmVirtualCamera stale-subscribe hook
// and asserts the reconnect still completes, quickly, and reaches a state where
// the shutter fires.
//
// Mutation check: revert Camera::gattSubscribe / Fujifilm::subscribe to an
// acknowledged CCCD write (response = true) and the stale-session peer rejects
// the write, so the Basic handshake aborts, connect() returns false, and this
// test fails. That is the test's tooth.

#include <chrono>
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

// A stale-session reconnect must complete, bounded, and reach a working shutter,
// never hang in the registration handshake.
bool testReconnectStaleSessionCompletes() {
  std::cout << "test: stale-session reconnect completes and fires the shutter (plan 76)\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);

  // First connect is a fresh session: the camera holds no prior subscriptions.
  if (!check(camera->connect(ESP_PWR_LVL_P3, 1000), "the first connect succeeds")) {
    NimBLEDevice::resetMock();
    return false;
  }
  check(camera->isConnected(), "the first connect reports connected");
  camera->disconnect();

  // Fast reconnect: the camera still holds the CCCD subscriptions, so an
  // acknowledged CCCD write would block or be rejected.
  peer.setStaleSubscribeSession(true);
  peer.clearEvents();

  const auto start = std::chrono::steady_clock::now();
  const bool reconnected = camera->connect(ESP_PWR_LVL_P3, 1000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(reconnected, "the stale-session reconnect completes instead of hanging");
  check(camera->isConnected(), "the stale-session reconnect reaches connected");
  check(elapsed < 5000, "the stale-session reconnect completes within a bounded time");

  // Reaching connected must mean the shutter is usable, mirroring the on-device
  // acceptance test where a shutter must fire after the reconnect.
  peer.clearEvents();
  camera->shutterPress();
  camera->shutterRelease();
  bool shutterFired = false;
  for (const auto &write : peer.writes()) {
    if (write.characteristic
        == Furble::Host::FujifilmVirtualCamera::shutterCharacteristicUUID().toString()) {
      shutterFired = true;
      break;
    }
  }
  check(shutterFired, "the shutter fires after the stale-session reconnect");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Guard the fresh first-connect path: the bounded unacknowledged subscribe must
// not regress a normal connect.
bool testFreshConnectStillWorks() {
  std::cout << "test: fresh first connect still reaches connected\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);

  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the fresh connect succeeds");
  check(camera->isConnected(), "the fresh connect reports connected");
  check(peer.connected() && peer.tokenAccepted(), "the fresh connect runs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testFreshConnectStillWorks();
  testReconnectStaleSessionCompletes();

  if (g_Failures != 0) {
    std::cout << "reconnect stuck harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "reconnect stuck harness: PASS\n";
  return 0;
}
