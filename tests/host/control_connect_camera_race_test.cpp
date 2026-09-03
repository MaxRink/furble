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
// The suppression file lists the pre-existing races this PR does not claim to
// fix, so a new one fails the run instead of hiding in the noise. TSAN's exit
// code is what fails the test: halt_on_error keeps the first report as the
// failure rather than letting the run continue past it.

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

// Polls the accessor the connect progress timer calls, which is the UI-task
// reader in production.
void pollConnectingCamera(void) {
  auto &control = Control::getInstance();
  while (g_PollRun.load()) {
    auto camera = control.getConnectingCamera();
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
  control.addActive(camera);
  control.connectAll(false);

  // Long enough for several publish and clear cycles on the control task while
  // the poller is copying the shared_ptr.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  control.disconnect();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  g_PollRun = false;
  poller.join();

  std::cout << "control-connect-camera-race: PASS\n";
  return 0;
}
