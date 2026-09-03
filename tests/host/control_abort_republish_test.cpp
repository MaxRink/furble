// Deterministic interleaving proof for the aborted-connect republish guard in
// Control::task() (plan 169).
//
// control-interleave (plan 157) proves the guard drops a stale
// STATE_DISCONNECTING. It cannot reach the other half of the same window.
// Control::disconnect() arms m_ConnectAbort one statement before it publishes
// STATE_DISCONNECTING, and connectAll() used to return m_State on the abort
// path, so a read landing between those two statements returned
// STATE_CONNECTING instead. The guard only refused STATE_DISCONNECTING, so the
// control task stamped STATE_CONNECTING back over the STATE_IDLE that
// disconnect() ends with. STATE_CONNECTING is as terminal for the control task
// as STATE_DISCONNECTING is (both fall through to `break`), so the machine
// ignored every later command until reboot.
//
// That is the failure control-e2e-flappy-cancel-stress caught by chance, as
// "iteration N late state connecting", at a rate near one run in fifty. This
// test forces the same interleaving exactly, with two barriers:
//
//   1. a peer that fails the pairing write makes every connect attempt fail
//      fast, so connectAll() parks in a reconnect retry wait that polls
//      m_ConnectAbort. Backoff is left enabled and the test waits for the
//      second retry, whose wait is 10 s, so nothing but the abort can end it
//      while the barriers below are armed,
//   2. a barrier on "disconnect_abort_armed" parks the disconnecting caller
//      with m_ConnectAbort set and STATE_CONNECTING still published, holding
//      the window open,
//   3. the retry wait sees the abort, connectAll() returns, and the control
//      task parks at "connectall_returned" holding that result,
//   4. the disconnect is released and runs to STATE_IDLE while the task is
//      still parked,
//   5. the task is released. The state must stay IDLE and the machine must
//      stay commandable.
//
// Mutation proof: restoring `return m_State;` on either abort path in
// Control::connectAll() makes step 5 fail, with the state wedged in
// STATE_CONNECTING and the follow-up connect never completing.

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

const char *LOG_TAG = "furble-abort-republish";

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

// m_ReconnectAttempt is incremented immediately before the interruptible retry
// wait, so this is the observable that says connectAll() is parked in the wait
// that polls m_ConnectAbort. Waiting for it rather than sleeping is what makes
// the interleaving below forced rather than raced.
bool waitForReconnectAttempt(uint32_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (control.getDebugState().reconnectAttempt >= want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getDebugState().reconnectAttempt >= want;
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;

  NimBLEDevice::resetMock();
  Furble::TestSync::reset();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  // Backoff on, so the second retry wait is 10 s rather than 5 s. Only the
  // abort may end the wait the barriers below are armed across.
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, true);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  // The peer accepts the link and rejects the pairing write, so every attempt
  // fails in milliseconds and the infinite reconnect drops straight into a
  // retry wait: 2.5 s for the first, then 10 s.
  FujifilmVirtualCamera peer;
  peer.failWrite(FujifilmVirtualCamera::pairServiceUUID(),
                 FujifilmVirtualCamera::pairCharacteristicUUID());
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  control.addActive(camera);
  check(control.getTargetCount() == 1, "one target after addActive");
  control.connectAll(true);

  // Step 1: connectAll() is parked in the retry wait, and STATE_CONNECTING is
  // the published state the abort window is about to be opened over.
  check(waitForReconnectAttempt(2, 20000), "the failed attempts enter the second retry wait");
  check(control.getState() == Control::STATE_CONNECTING,
        "the retry wait runs with STATE_CONNECTING published");

  // Step 2: hold the window open. The park timeout sits above
  // DISCONNECT_WAIT_MAX_MS (30 s) so a stalled teardown reports its own cause
  // rather than a sync point timeout.
  Furble::TestSync::armBarrier("disconnect_abort_armed", 35000);
  Furble::TestSync::armBarrier("connectall_returned", 35000);

  std::atomic<bool> completed {false};
  std::thread disconnector([&control, &completed] { completed = control.disconnect(); });

  const bool abortParked = check(Furble::TestSync::awaitArrival("disconnect_abort_armed", 10000),
                                 "disconnect parked with the abort armed");
  check(control.getState() == Control::STATE_CONNECTING,
        "STATE_DISCONNECTING is not published yet, so the window is really open");
  check(control.getDebugState().connectAbort, "the abort the retry wait polls is armed");

  // Step 3: the retry wait polls m_ConnectAbort, so connectAll() returns while
  // the window is still open and the control task parks holding its result.
  const bool taskParked = check(Furble::TestSync::awaitArrival("connectall_returned", 15000),
                                "control task parked at connectall_returned");

  // Step 4: let the disconnect finish while the task is parked. This is the
  // window: IDLE is published with a stale connect result still in flight.
  if (abortParked) {
    Furble::TestSync::release("disconnect_abort_armed");
  }
  disconnector.join();
  check(completed.load(), "interactive disconnect completes");
  check(control.getState() == Control::STATE_IDLE, "disconnect reached idle while task is parked");
  check(control.getTargetCount() == 0, "no live targets while task is parked");

  // Step 5: release the task. An aborted pass reports STATE_DISCONNECTING, so
  // the guard drops it. Returning m_State here instead stamps STATE_CONNECTING
  // over the IDLE above and wedges the machine.
  if (taskParked) {
    Furble::TestSync::release("connectall_returned");
  }
  check(!stateEverBecomes(Control::STATE_CONNECTING, 400),
        "no late CONNECTING republish after the aborted connect");
  check(control.getState() == Control::STATE_IDLE, "state is idle after the release");

  // A wedged machine ignores CMD_CONNECT forever, so prove it is commandable.
  peer.clearFaults();
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 15000), "follow-up connect reaches active");
  check(control.getConnectedTargetCount() == 1, "follow-up connect is connected");

  control.disconnect();
  check(waitForState(Control::STATE_IDLE, 5000), "final disconnect returns to idle");

  // A timed-out barrier means a parked thread was never released. That is a
  // test bug and must fail loudly, never hang the suite.
  check(!Furble::TestSync::anyTimedOut(), "no sync point timed out");

  if (g_Failures != 0) {
    std::cout << "control-abort-republish: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-abort-republish: PASS\n";
  return 0;
}
