// Targeted host regression test for BUG C: instant-false-connected on a fresh
// connect.
//
// A Camera in the persistent CameraList outlives its Control::Target. If a
// prior session left m_Connected true with no onDisconnect ever firing (camera
// powered off before the supervision timeout, or a partially established
// connect), the flag stays stale-true on the list object. With the lock-free
// isConnected() (a plain m_Connected read, no live client cross-check), the next
// connect sees isConnected() == true, Control::connectAll() skips the real
// connect, and furble reports connected with no BLE work done. A reboot rebuilds
// the CameraList with fresh objects, which is why a reboot clears it.
//
// The fix clears the flag on a fresh connect: Control::addActive() calls
// Camera::resetConnectionState() before wrapping the camera in a new Target.
// This test models the stale flag with a silent link drop (no disconnect
// callback) and asserts resetConnectionState() clears it so a fresh connect runs
// the real GATT handshake. Removing the resetConnectionState() body makes the
// clear assertion fail, which is the test's tooth.

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

constexpr int REASON_SUPERVISION_TIMEOUT = 0x08;

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// BUG C: resetConnectionState() clears a stale connected flag so a fresh connect
// does real work instead of short-circuiting.
bool testResetClearsStaleConnectedFlag() {
  std::cout << "test: resetConnectionState clears a stale connected flag (BUG C)\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  if (!check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera connects")) {
    NimBLEDevice::resetMock();
    return false;
  }

  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the camera created a client")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Camera powers off: the link is gone but onDisconnect has not run, so the
  // flag is left stale-true, exactly the state that survives a session on the
  // persistent CameraList object.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);

  // Precondition: the lock-free isConnected() cannot see the dead link, so the
  // stale flag reads connected. This is the BUG C state a fresh connect must not
  // trust.
  check(camera->isConnected(), "the stale flag reads connected before the reset");

  // Control::addActive() runs this on a fresh user connect.
  camera->resetConnectionState();
  check(!camera->isConnected(), "resetConnectionState clears the stale connected flag");

  // With the flag cleared, a fresh connect runs the real GATT handshake instead
  // of short-circuiting to connected.
  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the fresh connect establishes a real link");
  check(camera->isConnected(), "the fresh connect reports connected");
  check(peer.connected() && peer.tokenAccepted(), "the fresh connect performs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testResetClearsStaleConnectedFlag();

  if (g_Failures != 0) {
    std::cout << "stale connect harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "stale connect harness: PASS\n";
  return 0;
}
