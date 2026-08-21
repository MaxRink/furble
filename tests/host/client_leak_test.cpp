// Host regression test for the NimBLE client pool leak that broke every connect
// until a reboot.
//
// Root cause: Camera::connect() calls NimBLEDevice::createClient() and arms
// setSelfDelete(true, true). NimBLE only self-deletes the client when
// NimBLEClient::connect() is actually reached (deleteOnConnectFail) or when a
// disconnect event fires (deleteOnDisconnect). On a Fujifilm reconnect where the
// camera still holds its previous session, the pairing scan times out and the
// vendor _connect() returns false before NimBLEClient::connect() is ever called.
// No connect or disconnect event fires, so the client is never freed and is
// orphaned in the fixed-size pool (CONFIG_BT_NIMBLE_MAX_CONNECTIONS, 9 clients).
// After nine failed attempts createClient() returns null and every later connect
// fails ("Unable to create client; already at max: 9") until a reboot clears the
// pool.
//
// The fix reclaims the client in Camera::connect() on the failure path that
// never brought the link up: it calls NimBLEDevice::deleteClient(), which is a
// safe no-op when the client already self-deleted.
//
// This test drives repeated failed connects through the real Camera::connect()
// against MockNimBLE, which models the pool cap, a live-client count, and the
// deleteClient reclaim contract. setConnectShouldFail models the failure class
// that leaks on hardware: a connect that never establishes and never triggers
// self-delete. Removing the deleteClient() call in Camera::connect() makes the
// live count grow past one and exhausts the capped pool, so the assertions below
// fail. That is the test's tooth.

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

// The hardware pool cap, CONFIG_BT_NIMBLE_MAX_CONNECTIONS.
constexpr size_t NIMBLE_MAX_CLIENTS = 9;
// More attempts than the cap, so a leak provably exhausts the pool.
constexpr int FAILED_ATTEMPTS = 12;

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// Many consecutive failed connects must not leak clients or exhaust the pool.
bool testFailedConnectsDoNotLeakClients() {
  std::cout << "test: repeated failed connects do not leak NimBLE clients\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  // Cap the pool exactly as the controller does, so a leak surfaces as
  // exhaustion after the ninth failed attempt.
  NimBLEDevice::setMaxClients(NIMBLE_MAX_CLIENTS);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);

  // Every attempt fails the way a stale-session reconnect does on hardware.
  NimBLEDevice::setConnectShouldFail(true);
  bool bounded = true;
  for (int attempt = 0; attempt < FAILED_ATTEMPTS; attempt++) {
    check(!camera->connect(ESP_PWR_LVL_P3, 1000), "a failed connect returns false");
    // Each failed attempt must reclaim its client. Without the fix the count
    // climbs every attempt until it sticks at the pool cap.
    if (NimBLEDevice::liveClientCount() > 1) {
      bounded = false;
    }
  }
  check(bounded, "the live client count never grows past one across failed connects");
  check(NimBLEDevice::liveClientCount() == 0, "no clients remain live after the failed connects");

  // The pool is not exhausted, so a genuine connect still works. Before the fix
  // the ninth leak fills the pool and createClient() returns null here, so this
  // connect fails.
  NimBLEDevice::setConnectShouldFail(false);
  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000),
        "a real connect still succeeds after many failed attempts");
  check(camera->isConnected(), "the recovered connect reports connected");
  check(peer.connected() && peer.tokenAccepted(), "the recovered connect runs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// deleteClient() reclaims a live client and is a safe no-op on a client that was
// already reclaimed or self-deleted, so the fix cannot double free.
bool testDeleteClientIsIdempotent() {
  std::cout << "test: deleteClient reclaims once and is a safe no-op afterwards\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();

  NimBLEClient *client = NimBLEDevice::createClient();
  check(client != nullptr, "createClient returns a client");
  check(NimBLEDevice::liveClientCount() == 1, "one client is live after createClient");

  check(NimBLEDevice::deleteClient(client), "deleteClient reclaims the live client");
  check(NimBLEDevice::liveClientCount() == 0, "no clients are live after deleteClient");

  // A second delete of the same pointer models Camera::connect() reclaiming a
  // client that NimBLE already self-deleted. It must not double free.
  check(!NimBLEDevice::deleteClient(client), "a second deleteClient is a safe no-op");
  check(NimBLEDevice::liveClientCount() == 0, "the count stays at zero after the no-op");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testFailedConnectsDoNotLeakClients();
  testDeleteClientIsIdempotent();

  if (g_Failures != 0) {
    std::cout << "client leak harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "client leak harness: PASS\n";
  return 0;
}
