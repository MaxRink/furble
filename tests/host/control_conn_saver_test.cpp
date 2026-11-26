// Host regression for the connection-saver fan-out running on the LVGL task.
//
// Toggling the connection-saver setting calls Control::setConnSaver() from the
// UI switch handler, which runs on the LVGL task. That fan-out called
// Camera::setConnSaverEnabled() per target, and on a connected camera that
// enters NimBLE and issues a central connection-parameter update, which the
// host can hold on the HCI transport for up to two seconds. The screen and the
// M5PM1 watchdog, which is fed only from the UI task, stall behind it.
//
// A host test cannot measure that block, so it asserts the owner: the mock
// records the thread that issued each updateConnParams(), and this test
// requires that the caller's own thread never appears there. Red-first
// evidence: with the fan-out on the caller, setConnSaver() has already issued
// the final FAST request from this thread by the time it returns.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"

const char *LOG_TAG = "furble-host";

namespace {

using Furble::Control;

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

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

void ensureControlTask() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  // Through the shim, exactly as main() starts it on device: the shim owns the
  // thread and furbleHostStopTasks() joins it before this process exits.
  xTaskCreate(control_task, "control", 8192, &Control::getInstance(), 4, nullptr);
}

bool run(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);
  ensureControlTask();

  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  auto &control = Control::getInstance();
  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    return false;
  }
  if (!check(camera->connect(Furble::TxPower::P3, 1000), "the camera connects")) {
    return false;
  }

  // Turn the saver on first. The disable path only issues its final FAST
  // request when the last request was not already a successful FAST one, so
  // move the link to idle here and check the request was applied. Waiting for
  // maybeSetIdle() instead would take the ten second inactivity period.
  camera->setConnSaverEnabled(true);
  if (!check(camera->setConnProfile(Furble::Camera::ConnProfile::IDLE),
             "the saver's idle request is issued")) {
    return false;
  }
  if (!check(waitFor([&] { return camera->getConnProfile() == Furble::Camera::ConnProfile::IDLE; },
                     5000),
             "the link reports the idle profile")) {
    return false;
  }

  NimBLEDevice::mockClearConnParamThreads();
  control.setConnSaver(false);

  // The control task owns the fan-out, so the request appears from its thread.
  // The target task's own idle timer may add one too; neither is this thread,
  // which is the property the fix is about.
  const bool requested =
      waitFor([] { return !NimBLEDevice::mockConnParamThreads().empty(); }, 5000);
  const auto threads = NimBLEDevice::mockConnParamThreads();
  const bool callerIssued =
      std::find(threads.begin(), threads.end(), std::this_thread::get_id()) != threads.end();
  check(requested, "the connection-saver change reaches the radio");
  check(!callerIssued, "the connection-saver fan-out does not run on the caller's task");

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == 0;
}

}  // namespace

int main() {
  // The peer outlives the joined tasks: a target task still reading a camera
  // whose peer was already freed is the very use-after-free this harness must
  // not manufacture. Declaration order gives that, because destruction is the
  // reverse of it.
  Furble::Host::FujifilmVirtualCamera peer;
  // Stop and join every shim task before this scope ends, so no firmware task
  // is still running when static destruction frees what it reads.
  FurbleHostTaskScope taskScope;

  run(peer);

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "control-conn-saver: PASS\n";
  } else {
    std::cout << "control-conn-saver: FAIL (" << g_Failures << " checks)\n";
  }
  return status;
}
