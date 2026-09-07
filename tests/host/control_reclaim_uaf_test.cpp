// AddressSanitizer regression for the gone-peer client-reclaim use-after-free.
//
// The v2 reconnect-stall fix has Control reclaim a gone peer's orphaned NimBLE
// client at a short drain bound (Camera::reclaimClient()) instead of waiting the
// full backstop for an onDisconnect that never comes. The subtlety the code
// review caught: NimBLEDevice::deleteClient() on a still-CONNECTED client does
// not free it synchronously. The real stack sets deleteOnDisconnect and defers
// the free to the eventual onDisconnect, so the client outlives the Camera while
// still holding the raw callback pointer set at connect (setClientCallbacks(this)).
// The drained Camera is freed as soon as its target reaps and the next connect
// rebuilds the CameraList, so the late onDisconnect, fired seconds later when the
// supervision timeout finally resolves the terminate, would call
// Camera::onDisconnect() on freed memory.
//
// reclaimClient() closes this by detaching the client from the Camera
// (setClientCallbacks(nullptr)) before deleteClient(), so the late event lands on
// NimBLE's default no-op callbacks. This test drives that exact window with the
// mock's deferred-delete model and asserts no use-after-free.
//
// This binary is compiled with -fsanitize=address (Camera.cpp included, so the
// onDisconnect write is instrumented). With the detach, the late callback is a
// no-op and the test passes. Removing the setClientCallbacks(nullptr) line in
// Camera::reclaimClient() makes the late onDisconnect touch the freed Camera and
// ASan aborts, so the test is mutation proven.

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

constexpr int REASON_SUPERVISION_TIMEOUT = 0x08;

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

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

void ensureControlTask() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  // Through the shim, exactly as main() starts it on device: the shim owns the
  // thread and furbleHostStopTasks() joins it before this process exits.
  xTaskCreate(control_task, "control", 8192, &Furble::Control::getInstance(), 4, nullptr);
}

using Furble::Control;

// Connect a camera, stall its terminate (gone peer, client stays CONNECTED with
// onDisconnect pending), disconnect, let the control task reclaim the client at
// the drain bound, free the Camera, then fire the late onDisconnect and assert no
// use-after-free.
bool testReclaimDetachesClientBeforeDeferredDelete() {
  std::cout << "test: gone-peer reclaim detaches the client so a late onDisconnect is safe\n";
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

  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the camera created a client")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Gone peer with a stalled terminate: the client stays CONNECTED and no
  // onDisconnect fires. This is the state in which reclaimClient() runs, and the
  // one deleteClient() defers rather than frees.
  client->mockStallTerminate();
  check(camera->isConnected(), "the stalled terminate keeps the link reported up");

  control.disconnect();
  check(control.getState() == Control::STATE_IDLE, "the disconnect returns to idle at once");

  // The control task reaps the drained target only after reclaiming the orphaned
  // client at the drain bound (about 2 s). Our reference is the last one left.
  const bool reaped = waitFor([&] { return camera.use_count() == 1; }, 6000);
  check(reaped, "the gone-peer drain reclaims and reaps within the bound");

  // The client is deferred, not freed: still in the pool, so the deferred-delete
  // model is exercised (a synchronous free would not reproduce the late-callback
  // window this test guards).
  check(NimBLEDevice::liveClientCount() == 1, "deleteClient deferred the still-connected client");

  // Free the Camera as CameraList::load() does on the next connect.
  camera.reset();

  // The supervision timeout finally resolves: the late onDisconnect fires. With
  // the reclaim detach it lands on the default no-op callbacks; without it, it
  // would write through the freed Camera and ASan aborts here.
  client->mockCompleteStalledTerminate(REASON_SUPERVISION_TIMEOUT);
  check(true, "the late onDisconnect after reclaim is a no-op, not a use-after-free");
  check(NimBLEDevice::liveClientCount() == 0, "the deferred client frees on the late disconnect");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  // Stop and join every shim task before this scope ends, so no firmware task
  // is still running when static destruction frees what it reads. The control
  // task reaps the zombie drain on every 50 ms tick, so a thread left running
  // past ~Control walks a freed vector.
  FurbleHostTaskScope taskScope;

  testReclaimDetachesClientBeforeDeferredDelete();

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "control reclaim UAF harness: PASS\n";
  } else {
    std::cout << "control reclaim UAF harness: FAIL (" << g_Failures << " checks)\n";
  }

  return status;
}
