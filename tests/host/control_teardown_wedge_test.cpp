// Regression for the plan 148 teardown wedge.
//
// Hardware trace 2026-08-28 (X100VI, stale session): the camera keeps the BLE
// link up but withholds the registration confirmation, so every reconnect
// attempt parks inside Fujifilm::waitForRegistration() for the full 25 s while
// Camera::connect() holds Camera::m_Mutex. The attempt began while the camera
// was not active, so the isActive() cancel path was disabled and a user
// disconnect could not abort the wait. The target task's Camera::disconnect()
// blocked on the same mutex, targetTasksStopped() stayed false, the interactive
// disconnect burned toward its 30 s cap, and the drained target could never be
// reaped (a zombie is only reaped once its task stops). Control wedged in
// STATE_DISCONNECTING with connect_in_progress stuck true until reboot.
//
// This harness compiles the production FurbleControl.cpp and the real Fujifilm
// camera against MockNimBLE with a 10 s registration timeout, an order of
// magnitude above the asserted bounds, so the pre-fix behaviour fails the
// bounded-time checks deterministically:
//   1. saved camera, registration withheld, link stays up, terminate stalls
//      (the observed live-client state),
//   2. user disconnect() lands mid registration wait,
//   3. assert disconnect() returns bounded, Control reaches IDLE bounded, and
//      the drain gate opens again: a follow-up connect must reach ACTIVE,
//      proving the zombies drained to zero.
//
// With the fix, Control::disconnect() sets the per-camera connect cancel token
// and the registration wait aborts within one poll.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

const char *LOG_TAG = "furble-teardown-wedge";

namespace {

using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;

int g_Failures = 0;

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

uint32_t nowMs() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

bool waitForState(Control::state_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (control.getState() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() == want;
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

  FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  // The stale-session camera: link and GATT are healthy but the registration
  // confirmation never arrives. The camera is deliberately never marked active,
  // matching a reconnect attempt that begins inactive, so the legacy isActive()
  // cancel path cannot abort the wait: only the cancel token can.
  peer.setWithholdRegistration(true);

  control.addActive(camera);
  check(control.getTargetCount() == 1, "one target after addActive");
  control.connectAll(true);

  // Wait until the attempt has the link up and is parked in the registration
  // wait (10 s in this build).
  {
    const uint32_t start = nowMs();
    while (Furble::Host::timeoutPending(start, nowMs(), 5000) && !peer.connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  check(peer.connected(), "link is up while registration is withheld");

  // Model the observed hardware state: the camera holds the link, so the
  // failed-attempt terminate stalls and no onDisconnect ever fires. The live
  // NimBLE client seen on the wedged device.
  NimBLEClient *client = NimBLEDevice::lastClient();
  check(client != nullptr, "attempt created a client");
  if (client != nullptr) {
    client->mockStallTerminate();
  }

  // Let the control task sink into the registration wait proper.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // User disconnect lands mid-wait. Pre-fix this blocks for the remaining
  // registration timeout (~10 s here, 25 s per attempt on device) because
  // nothing can abort the wait, which is the wedge signature.
  const uint32_t start = nowMs();
  const bool completed = control.disconnect();
  const uint32_t elapsed = nowMs() - start;
  check(completed, "interactive disconnect completes");
  check(elapsed < 3000, "disconnect returns bounded, not after the registration timeout");
  check(waitForState(Control::STATE_IDLE, 2000), "control reaches idle after the cancel");
  check(control.getTargetCount() == 0, "no live targets after disconnect");

  // The drain gate must open again: the zombie's task stopped once the wait
  // aborted, so the drained target reaps and a follow-up connect goes through.
  // A wedged drain (unreapable zombie) would gate STATE_CONNECT forever.
  peer.setWithholdRegistration(false);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 8000), "follow-up connect reaches active");
  check(control.getConnectedTargetCount() == 1, "follow-up connect is connected");

  control.disconnect();
  check(waitForState(Control::STATE_IDLE, 3000), "final disconnect returns to idle");

  if (g_Failures != 0) {
    std::cout << "control-teardown-wedge: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-teardown-wedge: PASS\n";
  return 0;
}
