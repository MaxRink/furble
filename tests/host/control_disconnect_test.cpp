// Host regression test for the interactive disconnect UI freeze.
//
// This compiles the real src/FurbleControl.cpp against MockNimBLE, the real
// lib/furble Camera and the Fujifilm sources, and the host FreeRTOS shim under
// tests/host/control. It locks in the fix that made the interactive disconnect
// path (forRestart == false) non-blocking.
//
// The freeze it guards: a powered-off camera severs the BLE link but never
// fires onDisconnect within the interactive window, so Camera::isConnected()
// stays true. The old disconnect() polled that predicate on the caller (the
// LVGL/UI task) for up to DISCONNECT_WAIT_MAX_MS (30 s), so the screen wedged.
// The fix hands every target to the drain set and returns at once: getState()
// is STATE_IDLE and getTargetCount() is 0 the moment disconnect() returns, and
// the control task's reapZombieTargets() frees a drained target only once its
// task has stopped and the link is really down.
//
// Test A drives the dead-camera path and asserts the prompt return plus the
// gated drain that reaps only after the link goes down. Test B guards the
// common present-camera path.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"

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

// 0x08 is the connection supervision timeout, the reason a powered-off camera
// drops with. The mock only records it.
constexpr int REASON_SUPERVISION_TIMEOUT = 0x08;

// Poll a predicate until it holds or the timeout elapses. Keeps the reap
// assertions robust to the control task's 50 ms tick without a fixed sleep.
bool waitFor(const std::function<bool()> &predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

// Build a fresh camera advertising as the virtual Fujifilm peer. The peer stays
// owned by the caller for the lifetime of the test.
std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// Start the real control task once. It runs reapZombieTargets() every tick,
// which is what frees a drained target after its link goes down.
void ensureControlTask() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  std::thread(control_task, &Furble::Control::getInstance()).detach();
}

using Furble::Control;

// Test A: a powered-off camera. The interactive disconnect must return promptly
// and clear state, and the drained target must stay held until the dead link is
// really down, then reap.
bool testDeadCameraDisconnectReturnsPromptly() {
  std::cout << "test: dead-camera interactive disconnect returns promptly (BUG B)\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  // addActive creates the target and its task via the FreeRTOS shim.
  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Bring the link up through the real Camera connect path.
  if (!check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera connects")) {
    NimBLEDevice::resetMock();
    return false;
  }
  check(camera->isConnected(), "the camera reports connected");

  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the camera created a client")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Camera powered off: the link is severed but no onDisconnect fires, so the
  // stale connected flag persists. This is the freeze window.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);
  check(camera->isConnected(), "the stale connected flag persists after a silent drop");

  // The interactive disconnect must not block on the dead link.
  const auto start = std::chrono::steady_clock::now();
  control.disconnect();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(elapsed < 500, "the interactive disconnect of a dead camera returns promptly");
  check(control.getState() == Control::STATE_IDLE, "the state is idle immediately");
  check(control.getTargetCount() == 0, "no active targets remain immediately");

  // The drain is gated: the target is held while the dead link still reports up,
  // so a reconnect cannot race a client still being freed. Observe it through
  // the camera reference count: our reference plus the drained target's is two.
  check(camera->isConnected(), "the dead link still reports connected right after disconnect");
  check(camera.use_count() == 2, "the drained target still holds the camera, gating reconnect");

  // Deliver onDisconnect: the link is really down now.
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/true);
  const bool reaped = waitFor([&] { return camera.use_count() == 1; }, 3000);
  check(reaped, "the drain reaps the target once the link is really down");
  check(!camera->isConnected(), "the camera reports disconnected after onDisconnect");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Test B: a present camera. The common interactive disconnect path still
// completes, leaving the controller idle with no targets, and the drain clears.
bool testCleanDisconnectLeavesIdle() {
  std::cout << "test: clean interactive disconnect leaves idle with no targets\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }
  if (!check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera connects")) {
    NimBLEDevice::resetMock();
    return false;
  }
  check(camera->isConnected(), "the camera reports connected");

  const auto start = std::chrono::steady_clock::now();
  control.disconnect();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(elapsed < 500, "the clean interactive disconnect returns promptly");
  check(control.getState() == Control::STATE_IDLE, "the state is idle immediately");
  check(control.getTargetCount() == 0, "no active targets remain immediately");

  // A present camera fires onDisconnect during its own teardown, so the link is
  // down and the drain clears without any further prompting.
  const bool reaped = waitFor([&] { return camera.use_count() == 1; }, 3000);
  check(reaped, "the drain clears once the clean teardown completes");
  check(!camera->isConnected(), "the camera reports disconnected afterwards");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Test C: connect -> dead-camera disconnect -> immediate reconnect. A gone peer
// never fires onDisconnect, so its drained target would sit until the backstop.
// The reconnect must not be gated that whole time: the control task reclaims the
// orphaned client at the short drain bound, releasing teardownDraining(), so the
// fresh connect completes promptly. This guards the hardware regression where a
// `connect` right after a dead-camera Disconnect stuck in "connecting" for 30 s.
bool testReconnectAfterDeadDisconnectIsPrompt() {
  std::cout << "test: reconnect after a dead-camera disconnect is not gated ~30 s\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }
  if (!check(camera->connect(ESP_PWR_LVL_P3, 1000), "the camera connects")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Camera powered off: severed link, no onDisconnect, so the client is orphaned
  // and will never self-delete. This is the gone peer that stalled the reconnect.
  NimBLEDevice::lastClient()->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/false);
  control.disconnect();
  check(control.getState() == Control::STATE_IDLE, "the dead disconnect returns to idle");
  check(control.getTargetCount() == 0, "no active targets remain after the dead disconnect");

  // Reconnect immediately with a fresh camera, exactly as a `connect` after
  // Disconnect does (CameraList rebuilds the camera). Never deliver onDisconnect
  // for the old link: the reclaim must release the gate on its own.
  auto reconnected = makeCamera(peer);
  control.addActive(reconnected);
  const auto start = std::chrono::steady_clock::now();
  control.connectAll(false);

  // The fresh connect must reach active well within the 30 s backstop. With the
  // reclaim it clears the gate at the drain bound (about 2 s); without it the
  // gate holds for the full 30 s and this times out.
  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 8000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(connected, "the reconnect completes instead of stalling behind the dead-peer drain");
  check(elapsed < 8000, "the reconnect is not gated for the ~30 s backstop");
  std::cout << "  reconnect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testDeadCameraDisconnectReturnsPromptly();
  testCleanDisconnectLeavesIdle();
  testReconnectAfterDeadDisconnectIsPrompt();

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "control disconnect harness: PASS\n";
  } else {
    std::cout << "control disconnect harness: FAIL (" << g_Failures << " checks)\n";
  }

  // The control task runs for the whole process and is never joined. Flush and
  // exit immediately so its detached thread is not torn down against destroyed
  // singletons on return from main.
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(status);
}
