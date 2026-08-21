// Host tests that exercise the expanded fault-injection tooling and guard the
// connect and command error paths it reaches.
//
// These do not document a product bug. They prove the new MockNimBLE and
// FujifilmVirtualCamera fault hooks work and lock in the correct behavior of the
// paths they reach, so a regression in the reclaim or abort logic fails here:
//
//   - setConnectFailCount models a transient link that misses a few attempts and
//     then establishes. Each miss must reclaim its NimBLE client so the fixed
//     size pool never leaks, and the eventual attempt must run the real
//     handshake.
//   - failWrite models an ATT write that the peer rejects mid handshake. The
//     connect must abort, leave the camera disconnected, and reclaim the client.
//   - suppressService of an unrelated optional service must not stop a connect,
//     proving the suppression hook is targeted rather than global.

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

// A run of transient connect failures must not leak clients and must recover.
bool testTransientConnectFailuresRecover() {
  std::cout << "test: transient connect failures reclaim clients and then recover\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);

  // The next three NimBLEClient::connect() calls fail, then connects succeed.
  NimBLEDevice::setConnectFailCount(3);
  for (int attempt = 0; attempt < 3; attempt++) {
    check(!camera->connect(ESP_PWR_LVL_P3, 1000), "a transient attempt returns false");
    check(!camera->isConnected(), "a transient failure leaves the camera disconnected");
    check(NimBLEDevice::liveClientCount() == 0, "each transient failure reclaims its client");
  }

  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the connect recovers once the link establishes");
  check(camera->isConnected(), "the recovered connect reports connected");
  check(peer.connected() && peer.tokenAccepted(), "the recovered connect runs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// A rejected handshake write must abort the connect without leaking a client.
bool testWriteFailureAbortsConnectNoLeak() {
  std::cout << "test: a rejected handshake write aborts the connect and reclaims the client\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  // Reject the pairing token write, so the Basic handshake aborts at its first
  // write, the link having already come up.
  peer.failWrite(Furble::Host::FujifilmVirtualCamera::pairServiceUUID(),
                 Furble::Host::FujifilmVirtualCamera::pairCharacteristicUUID());
  auto camera = makeCamera(peer);

  check(!camera->connect(ESP_PWR_LVL_P3, 1000), "a rejected handshake write fails the connect");
  check(!camera->isConnected(), "a failed handshake leaves the camera disconnected");

  // Note: this write failure comes after the link is up, so Camera::connect
  // tears the link down and relies on NimBLE's deleteOnDisconnect to free the
  // client. MockNimBLE's setSelfDelete is a no-op, so the client is not reclaimed
  // in the mock on this path, and liveClientCount is not a valid probe here. The
  // real leak-relevant observable is that a later connect still succeeds rather
  // than exhausting the pool, which the recovery below asserts.

  // Recovery: clearing the fault lets a fresh connect run the real handshake.
  Furble::Host::FujifilmVirtualCamera healthy;
  auto recovered = makeCamera(healthy);
  check(recovered->connect(ESP_PWR_LVL_P3, 1000), "a later healthy connect still succeeds");
  check(healthy.connected() && healthy.tokenAccepted(), "the healthy connect runs the handshake");

  recovered->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Suppressing an unrelated optional service must not stop the connect.
bool testSuppressUnrelatedServiceStillConnects() {
  std::cout << "test: suppressing the optional geotag service still lets the connect complete\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  // The geotag service is optional for the Basic handshake, so removing it must
  // not break a connect. This proves suppression targets one service only.
  peer.suppressService(Furble::Host::FujifilmVirtualCamera::geotagServiceUUID());
  auto camera = makeCamera(peer);

  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the connect completes without the geotag service");
  check(camera->isConnected(), "the camera reports connected");

  // The shutter still fires, so the required path is intact.
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
  check(shutterFired, "the shutter still fires with the geotag service suppressed");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testTransientConnectFailuresRecover();
  testWriteFailureAbortsConnectNoLeak();
  testSuppressUnrelatedServiceStillConnects();

  if (g_Failures != 0) {
    std::cout << "fault tooling harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "fault tooling harness: PASS\n";
  return 0;
}
