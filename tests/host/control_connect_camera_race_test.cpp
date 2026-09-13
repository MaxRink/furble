// Thread-sanitizer proof for the Control::m_ConnectCamera guard.
//
// The field is written by the control task inside connectAll() and read by the
// UI task through getConnectingCamera(), which the connect progress timer calls
// (src/FurbleUI.cpp). Before this PR the write was unlocked and the getter was
// unlocked, while disconnect() and getDebugState() read it under m_Mutex. That
// is a data race on a shared_ptr: the racing copy touches the control block, so
// a torn read hands out a block that is being replaced.
//
// The PR review reproduced this deterministically rather than by argument, so
// the claim that it is only inspectable is retired. This test is that
// reproduction: run a connect cycle while a second thread polls the getter,
// under -fsanitize=thread. Guarded, TSAN reports nothing in
// Control::getConnectingCamera; with the guard reverted it reports a race on
// the shared_ptr control block under _M_add_ref_copy.
//
// run_tsan_race.sh asserts that specific claim rather than "zero races". The
// races this PR does not fix have template top frames, so any suppression broad
// enough to silence them would also hide the one being proved, and there is no
// suppression file. The wrapper runs with halt_on_error=0 so every report is
// collected, tolerates the sanitizer exit code 66 for "races were found", and
// fails only when a report names the guarded accessor. The remaining count is
// printed for visibility, not asserted.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FauxNY.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

const char *LOG_TAG = "furble-connect-camera-race";

namespace {

using Furble::Control;

std::atomic<bool> g_PollRun {true};
std::atomic<uint64_t> g_DebugObservations {0};

bool waitForState(Control &control, Control::state_t wanted, uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (control.getState() == wanted) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return control.getState() == wanted;
}

// Polls the accessor the connect progress timer calls, which is the UI-task
// reader in production.
void pollConnectingCamera(void) {
  auto &control = Control::getInstance();
  while (g_PollRun.load()) {
    auto camera = control.getConnectingCamera();
    const auto snapshot = control.getDebugState();
    g_DebugObservations.fetch_add(
        snapshot.targetCount + snapshot.connectedCount + snapshot.zombieCount
            + snapshot.reconnectAttempt + static_cast<size_t>(snapshot.state)
            + static_cast<size_t>(snapshot.connectInProgress)
            + static_cast<size_t>(snapshot.connectAbort)
            + static_cast<size_t>(snapshot.sleepLockHeld)
            + static_cast<size_t>(snapshot.infiniteReconnect)
            + static_cast<size_t>(snapshot.reconnectBackoff)
            + static_cast<size_t>(snapshot.adaptiveActive)
            + static_cast<size_t>(snapshot.userPowerLevel)
            + static_cast<size_t>(snapshot.adaptivePowerLevel) + snapshot.rssiStrongSamples
            + snapshot.rssiWeakSamples + snapshot.connectingCamera.size()
            + snapshot.connectFailReason.size(),
        std::memory_order_relaxed);
    if (camera != nullptr) {
      // Touch the pointee so the copy is not optimised away.
      volatile size_t len = camera->getName().size();
      (void)len;
    }
  }
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  std::thread poller(pollConnectingCamera);

  auto camera = std::make_shared<Furble::FauxNY>();
  bool passed = true;
  for (int cycle = 0; cycle < 2; ++cycle) {
    control.addActive(camera);
    control.connectAll(false);
    if (!waitForState(control, Control::STATE_ACTIVE, 5000)) {
      std::cerr << "connect cycle did not reach ACTIVE\n";
      passed = false;
      break;
    }
    if (!control.disconnect() || !waitForState(control, Control::STATE_IDLE, 5000)) {
      std::cerr << "disconnect cycle did not complete\n";
      passed = false;
      break;
    }
  }

  g_PollRun.store(false);
  poller.join();

  if (!passed || g_DebugObservations.load() == 0) {
    return 1;
  }
  std::cout << "control-connect-camera-race: PASS\n";
  return 0;
}
