// Deterministic interleaving proof for the STATE_DISCONNECTING republish
// guard in Control::task() (plan 157).
//
// The 2026-08-28 test gap analysis found the host harness had no
// deterministic interleaving control over the control task, so sub-50 ms
// races were unforceable. Concrete demonstration: mutating away the
// republish guard in Control::task() (the `next != STATE_DISCONNECTING`
// condition after connectAll() returns) could not be made to fail any test,
// because forcing the wedge needs a precise preemption between connectAll()
// returning and the setState() call. That guard is exactly what prevented
// the hardware wedge class: disconnect() moves the machine to IDLE in that
// window, and stamping DISCONNECTING back afterwards is terminal for the
// control task, wedging the device until reboot.
//
// This test uses the FURBLE_TEST_SYNC hook to force the exact interleaving:
//   1. a saved camera withholds registration, so the connect attempt parks
//      in the registration wait (10 s in this build),
//   2. a barrier is armed on "connectall_returned" and a user disconnect()
//      lands mid-wait; the cancel unwinds the attempt, connectAll() returns
//      STATE_DISCONNECTING, and the control task parks at the barrier
//      holding that stale result,
//   3. disconnect() completes to STATE_IDLE while the control task is still
//      parked, which is the wedge window,
//   4. the barrier is released and the guard must refuse the late republish:
//      the state stays IDLE and the machine stays commandable (a follow-up
//      connect reaches ACTIVE).
//
// Mutation proof: removing the `next != STATE_DISCONNECTING` guard makes
// this test fail at step 4 (the state machine wedges in DISCONNECTING and
// the follow-up connect never completes). The other two registry points are
// exercised as callbacks so a renamed or dropped point fails here too.

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
#include "TestSyncController.h"
#include "WrapSafeTime.h"

const char *LOG_TAG = "furble-interleave";

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

// True if the state ever reads `bad` within window_ms.
bool stateEverBecomes(Control::state_t bad, uint32_t window_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), window_ms)) {
    if (control.getState() == bad) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() == bad;
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;

  NimBLEDevice::resetMock();
  Furble::TestSync::reset();
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

  // Observe the two auxiliary registry points so a rename or removal fails
  // the suite. They must not perturb timing, so they only count.
  std::atomic<int> abortArmedCount {0};
  std::atomic<int> connectDequeuedCount {0};
  Furble::TestSync::onPoint("disconnect_abort_armed", [&abortArmedCount] { abortArmedCount++; });
  Furble::TestSync::onPoint("idle_connect_dequeued",
                            [&connectDequeuedCount] { connectDequeuedCount++; });

  // Step 1: park the connect attempt in the registration wait.
  peer.setWithholdRegistration(true);
  control.addActive(camera);
  check(control.getTargetCount() == 1, "one target after addActive");
  control.connectAll(true);

  {
    const uint32_t start = nowMs();
    while (Furble::Host::timeoutPending(start, nowMs(), 5000) && !peer.connected()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  check(peer.connected(), "link is up while registration is withheld");
  check(connectDequeuedCount.load() == 1, "idle_connect_dequeued fired for the first connect");

  // Let the control task sink into the registration wait proper.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Step 2: arm the wedge window, then land the user disconnect mid-wait.
  // The cancel token unwinds the attempt, connectAll() observes
  // STATE_DISCONNECTING and returns it, and the control task parks at the
  // barrier holding that stale result.
  //
  // The park timeout is deliberately above Control::DISCONNECT_WAIT_MAX_MS
  // (30 s). A teardown that stalls must surface as its own failure rather
  // than as a sync point timeout blamed on this test.
  Furble::TestSync::armBarrier("connectall_returned", 35000);

  // disconnect() runs on a worker so the main thread can observe the park
  // while the teardown is still in flight, which is the only moment the
  // premise of this test is directly checkable (see step 3).
  std::atomic<bool> completed {false};
  std::thread disconnector([&control, &completed] { completed = control.disconnect(); });

  // Step 3: the control task must park at connectall_returned holding the
  // stale DISCONNECTING result, which is the exact preemption the gap
  // analysis could not force.
  const bool parked = check(Furble::TestSync::awaitArrival("connectall_returned", 5000),
                            "control task parked at connectall_returned");

  // Verify the premise rather than relying on the later checks to be
  // fail-safe. connectAll() returns m_State on the abort path, so the value
  // the parked task is holding is whatever the state read moments before the
  // point fired. If that already read IDLE the guard below is never
  // exercised and this test would pass vacuously. disconnect() cannot
  // publish IDLE until targetTasksStopped(), which it polls in
  // DISCONNECT_WAIT_SLICE_MS slices after the target task has taken the
  // camera mutex the unwinding connect just released, so the park lands
  // inside the DISCONNECTING window with a wide margin.
  if (parked) {
    check(control.getState() == Control::STATE_DISCONNECTING,
          "connectAll returned the DISCONNECTING result the guard must drop");
  }

  disconnector.join();
  check(completed.load(), "interactive disconnect completes");
  check(abortArmedCount.load() == 1, "disconnect_abort_armed fired for the disconnect");
  check(control.getState() == Control::STATE_IDLE, "disconnect reached idle while task is parked");
  check(control.getTargetCount() == 0, "no live targets while task is parked");

  // Step 4: release the park. The republish guard must drop the stale
  // DISCONNECTING result. Without the guard the state machine wedges in
  // DISCONNECTING (terminal for the control task) until reboot.
  Furble::TestSync::release("connectall_returned");
  check(!stateEverBecomes(Control::STATE_DISCONNECTING, 400),
        "guard prevents the late DISCONNECTING republish, state stays idle");
  check(control.getState() == Control::STATE_IDLE, "state is idle after the release");

  // The machine must still be commandable: a follow-up connect reaches
  // ACTIVE. A wedged machine ignores CMD_CONNECT forever.
  peer.setWithholdRegistration(false);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 8000), "follow-up connect reaches active");
  check(control.getConnectedTargetCount() == 1, "follow-up connect is connected");
  check(connectDequeuedCount.load() == 2, "idle_connect_dequeued fired for the follow-up connect");

  control.disconnect();
  check(waitForState(Control::STATE_IDLE, 3000), "final disconnect returns to idle");
  check(abortArmedCount.load() == 2, "disconnect_abort_armed fired for the final disconnect");

  // A timed-out barrier means a parked thread was never released. That is a
  // test bug and must fail loudly, never hang the suite.
  check(!Furble::TestSync::anyTimedOut(), "no sync point timed out");

  if (g_Failures != 0) {
    std::cout << "control-interleave: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-interleave: PASS\n";
  return 0;
}
