// Host regression tests for the camera connect and disconnect lifecycle.
//
// These build the real lib/furble Camera against MockNimBLE and drive the
// transitions that only used to show up on hardware: a false-connected stale
// state, a link drop, a connect failure, and a peer that vanishes. Each test
// asserts the corrected behavior, so a future change that reintroduces one of
// the hardware bugs fails here instead of on a bench.
//
// Bugs covered:
//   - #106  CameraList use-after-free (shared ownership keeps a held Camera
//           alive across a list clear).
//   - BUG C  false-connected on a stale flag (isConnected() must consult the
//            live client, not just the cached m_Connected).
//   - BUG B  disconnect on a dead link completes promptly (Camera level).
//   - BUG A  onDisconnect clears the connected state.
//   - camera off / supervision timeout eventually clears state.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

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

// Reason codes taken from real captures. 0x08 is the connection supervision
// timeout, 0x13 is a remote-terminated link. The mock only records them.
constexpr int REASON_SUPERVISION_TIMEOUT = 0x08;
constexpr int REASON_REMOTE_TERMINATED = 0x13;

// Build a fresh camera advertising as the virtual Fujifilm peer. The peer stays
// owned by the caller for the lifetime of the test.
std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// Regression test 1: #106 CameraList use-after-free.
//
// A connect holds a Camera the way Control::addActive() and m_ConnectCamera do,
// then the saved-camera list is cleared the way CameraList::load()/clear()
// does. Before #106 the list uniquely owned the Camera, so clearing it freed
// the object out from under the in-flight connect. Shared ownership keeps it
// alive. This test would have caught #106.
bool testSharedOwnershipSurvivesListClear() {
  std::cout << "test: shared ownership survives a camera list clear (#106)\n";
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;

  // The saved-camera list owns the Camera.
  std::vector<std::shared_ptr<Furble::Camera>> list;
  list.push_back(makeCamera(peer));

  // Control takes its own strong reference, then starts the connection.
  std::shared_ptr<Furble::Camera> held = list.back();
  if (!check(held->connect(ESP_PWR_LVL_P3, 1000), "the held camera connects")) {
    return false;
  }

  // CameraList::load()/clear() drops the list's reference mid-session.
  list.clear();

  // The Camera the connect target holds must still be alive and valid.
  if (!check(held.use_count() == 1, "the target keeps the only strong reference")) {
    return false;
  }
  if (!check(held->isConnected(), "the held camera is still connected and valid")) {
    return false;
  }

  held->disconnect();
  check(!held->isConnected(), "the held camera disconnects cleanly");
  NimBLEDevice::resetMock();
  return g_Failures == 0;
}

// Regression test 2a: a failed connect must not report connected, and a later
// connect must do real work rather than short-circuit.
bool testFailedConnectDoesNotReportConnected() {
  std::cout << "test: a failed connect stays disconnected and retries for real\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);

  NimBLEDevice::setConnectShouldFail(true);
  check(!camera->connect(ESP_PWR_LVL_P3, 1000), "a connect that never establishes returns false");
  check(!camera->isConnected(), "a failed connect leaves the camera disconnected");
  check(!peer.connected(), "the peer sees no session for a failed connect");

  // A fresh connect must run the real GATT handshake, not report connected off
  // a stale flag.
  NimBLEDevice::setConnectShouldFail(false);
  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000), "a later connect establishes a real link");
  check(camera->isConnected(), "the retry reports connected");
  check(peer.connected() && peer.tokenAccepted(), "the retry performs the real GATT handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Regression test 2b: false-connected on a stale flag (BUG C).
//
// The camera powers off. The stack has not yet fired onDisconnect, so the
// Camera still holds m_Connected == true, but the live client link is down.
// isConnected() must consult the live client and report false. If a refactor
// drops the m_Client->isConnected() cross-check, this test fails.
bool testStaleConnectedFlagDoesNotReportConnected() {
  std::cout << "test: a stale connected flag does not mask a dead link (BUG C)\n";
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

  // Silent link loss: the link is down but onDisconnect has not run, so the
  // Camera still believes it is connected.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);
  check(!camera->isConnected(), "a stale connected flag does not report connected");
  check(camera->getRssi() == 0, "no RSSI is read from a dead link");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Regression test 3: disconnect on a dead link completes promptly (BUG B).
//
// After a silent link loss the Camera flag is stale-true. Camera::disconnect()
// must return promptly without dereferencing the freed client, and the liveness
// predicate that Control::disconnectComplete() polls (camera->isConnected())
// must read false at once so the interactive disconnect never spins to its
// 30 s backstop.
bool testDisconnectOnDeadLinkCompletesPromptly() {
  std::cout << "test: disconnect on a dead link completes promptly (BUG B)\n";
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
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);

  // The predicate Control::disconnectComplete() polls must read false at once.
  check(!camera->isConnected(), "the dead link reports disconnected before teardown");

  const auto start = std::chrono::steady_clock::now();
  camera->disconnect();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(elapsed < 1000, "disconnect on a dead link returns promptly");
  check(!camera->isConnected(), "the camera is disconnected afterwards");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Regression test 4: onDisconnect clears the connected state (BUG A).
//
// The stack detects the drop and fires onDisconnect. The Camera must clear its
// connected state, and a later connect must perform the real handshake rather
// than short-circuit off a leftover flag.
bool testOnDisconnectClearsState() {
  std::cout << "test: onDisconnect clears the connected state (BUG A)\n";
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
  client->mockDropLink(REASON_REMOTE_TERMINATED, /*fire_callback=*/true);
  check(!camera->isConnected(), "onDisconnect clears the connected state");

  // The connected state is fully cleared, so a reconnect runs real work.
  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera reconnects after a drop");
  check(peer.connected() && peer.tokenAccepted(), "the reconnect runs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Regression test 5: camera off / supervision timeout eventually clears state.
//
// The camera powers off, so the link is silently gone first. Later the
// supervision timeout elapses and the stack fires onDisconnect. Neither window
// may report connected, and the final state must be fully cleared.
bool testSupervisionTimeoutClearsState() {
  std::cout << "test: a camera-off supervision timeout clears state\n";
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

  // Camera powers off: the link is gone before the stack notices.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);
  check(!camera->isConnected(), "the silent dead link is not reported as connected");

  // The supervision timeout finally elapses and the stack fires onDisconnect.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/true);
  check(!camera->isConnected(), "state stays cleared after the timeout fires");
  check(camera->getRssi() == 0, "no RSSI is read from a dead link");

  // Recovery: a later connect still works after the peer comes back.
  peer.clearEvents();
  check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera reconnects once the peer returns");
  check(peer.connected() && peer.tokenAccepted(), "the recovery runs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testSharedOwnershipSurvivesListClear();
  testFailedConnectDoesNotReportConnected();
  testStaleConnectedFlagDoesNotReportConnected();
  testDisconnectOnDeadLinkCompletesPromptly();
  testOnDisconnectClearsState();
  testSupervisionTimeoutClearsState();

  if (g_Failures != 0) {
    std::cout << "camera lifecycle harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "camera lifecycle harness: PASS\n";
  return 0;
}
