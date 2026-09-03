// Regression for issue 271: a draining target whose connect attempt is still in
// flight.
//
// The reachable route, on hardware, starts with a wait inside a vendor connect
// that does not poll the plan 148 cancel token. CanonEOSSmart::_connect() waited
// 60 s for a pairing confirmation without polling, twice
// Control::DISCONNECT_WAIT_MAX_MS, and the Fujifilm Secure stale-bond
// secureConnection() window blocks inside NimBLE, which is not a poll at all. A
// user disconnect during one of those cannot abort the attempt, so the teardown
// burns its whole cap and drains the target while the attempt still holds
// Camera::m_Mutex.
//
// What made that harmful was the re-arm. Control::connectAll(bool) runs on the
// UI task and cleared every target camera's cancel token, and
// Control::connectAll() works from a snapshot taken before the drain, so the
// token was cleared out from under an attempt that was still running. The
// attempt became uncancellable for the rest of its vendor timeout, its target
// task stayed blocked on the same mutex so the drained target never published
// m_Stopped, and teardownDraining() held the connect gate shut.
//
// The fix moves the re-arm to the control task, at the top of the cycle, where
// no attempt can be in flight because an in-flight attempt runs inside that same
// function on that same task. Nothing is refused and no target is dropped.
//
// An earlier revision of this PR refused a draining camera a fresh target
// instead. That was withdrawn: with the only selected camera refused, m_Targets
// was empty, allConnected() was vacuously true, and Control published
// STATE_ACTIVE for a session containing nothing, so the UI signalled CONNECTED
// while shutter did nothing. Phase 2 issues the production sequence that
// exposed it (addActive then connectAll, which is what both entry points do) and
// a probe thread watches for that state throughout the run.
//
// The camera models the pre-fix Canon shape directly rather than through a
// vendor peer: a blind window inside _connect() that ignores the cancel token,
// then a polling window that honours it. Every bound is milliseconds so the
// pre-fix behaviour fails deterministically without waiting out a real vendor
// timeout. The bounds are wall clock and deliberately generous, roughly a factor
// of two against the behaviour they separate, so a loaded host does not flip
// them.

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

const char *LOG_TAG = "furble-zombie-cancel";

namespace {

using Furble::Control;

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

// A camera whose connect has a blind window that ignores the cancel token,
// followed by a polling window that honours it. The blind window is the
// pre-fix CanonEOSSmart pairing wait in miniature.
class BlindConfirmCamera: public Furble::FauxNY {
 public:
  BlindConfirmCamera(uint32_t blindMs, uint32_t pollMs)
      : Furble::FauxNY(), m_BlindMs {blindMs}, m_PollMs {pollMs} {}

  bool inAttempt(void) const { return m_InAttempt.load(); }
  bool cancelObserved(void) const { return m_CancelObserved.load(); }
  uint32_t attemptEndedMs(void) const { return m_AttemptEndedMs.load(); }

 protected:
  bool _connect(void) override {
    m_InAttempt = true;
    m_CancelObserved = false;

    // Blind window: no cancel poll at all, the defect being modelled.
    for (uint32_t elapsed = 0; elapsed < m_BlindMs; elapsed += SLICE_MS) {
      vTaskDelay(pdMS_TO_TICKS(SLICE_MS));
    }

    // Polling window: the plan 148 contract, honoured.
    for (uint32_t elapsed = 0; elapsed < m_PollMs; elapsed += SLICE_MS) {
      if (connectCancelled()) {
        m_CancelObserved = true;
        m_AttemptEndedMs = nowMs();
        m_InAttempt = false;
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(SLICE_MS));
    }

    m_AttemptEndedMs = nowMs();
    m_InAttempt = false;
    return false;
  }

 private:
  static constexpr uint32_t SLICE_MS = 10;
  const uint32_t m_BlindMs;
  const uint32_t m_PollMs;
  std::atomic<bool> m_InAttempt {false};
  std::atomic<bool> m_CancelObserved {false};
  std::atomic<uint32_t> m_AttemptEndedMs {0};
};

// Watches for the withdrawn design's failure mode for the whole run: Control
// publishing STATE_ACTIVE for a session with no targets. sendCommand() iterates
// m_Targets, so that state is a connected-looking UI with a dead shutter.
std::atomic<bool> g_ProbeRun {true};
std::atomic<bool> g_PhantomActive {false};

void phantomProbe(void) {
  auto &control = Control::getInstance();
  while (g_ProbeRun.load()) {
    if (control.getState() == Control::STATE_ACTIVE && control.getTargetCount() == 0) {
      g_PhantomActive = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

bool waitFor(const std::function<bool()> &predicate, uint32_t timeout_ms) {
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
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
  std::thread probe(phantomProbe);

  // 1500 ms blind, then 1500 ms polling. The blind window is long enough that
  // the pre-fix interactive cap cannot expire inside it by accident, and short
  // enough that the whole test is seconds.
  auto camera = std::make_shared<BlindConfirmCamera>(1500, 1500);

  control.addActive(camera);
  check(control.getTargetCount() == 1, "one target after addActive");
  control.connectAll(true);

  check(waitFor([&]() { return camera->inAttempt(); }, 2000),
        "the connect attempt reaches the blind confirmation window");

  // Phase 1. The caller asks for a 50 ms cap while the attempt is blind. Pre-fix
  // the parameter was ignored and this waited for the blind window to end.
  const uint32_t start = nowMs();
  control.disconnect(50);
  const uint32_t elapsed = nowMs() - start;
  check(elapsed < 600, "interactive disconnect honours its 50 ms cap, not the blind wait");
  check(control.getTargetCount() == 0, "the target drained");
  check(camera->inAttempt(), "the attempt is still in flight, which is the drained state");

  // Phase 2. The production sequence: addActive() for the camera, then
  // connectAll(), which is exactly what both connect entry points do. The camera
  // must be added, because dropping it is what produced the phantom active
  // session, and the attempt already in flight must keep the cancel token the
  // teardown set rather than having it cleared out from under it.
  control.addActive(camera);
  check(control.getTargetCount() == 1, "the draining camera is added, not dropped");
  control.connectAll(true);

  // The in-flight attempt reaches its polling window and sees the cancel. If the
  // re-arm still ran on the UI task the token would be gone by now and the
  // attempt would run its whole polling window out instead.
  check(waitFor([&]() { return camera->cancelObserved(); }, 3000),
        "the in-flight attempt still observes the cancel after a new connect cycle");

  check(!g_PhantomActive.load(), "control never published active with no targets");

  // Close phase 2's session. connectAll(true) armed the infinite reconnect, so
  // without this the control task keeps retrying and the next phase would stack
  // a second target rather than starting clean.
  control.disconnect();
  check(waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, 4000),
        "control returns to idle after the production sequence");
  check(control.getTargetCount() == 0, "no targets after the production sequence");

  // Phase 3. The teardown must not depend on a token an earlier call happened to
  // set. Drain the camera again, clear the token exactly as a pre-fix connect
  // cycle did, and require the next disconnect to cancel the attempt anyway.
  // Let the first camera's session go fully quiet first. A drained target is
  // only reaped once its attempt ends, and teardownDraining() gates
  // STATE_CONNECT until it is, so starting the next phase early would time out
  // on the gate rather than on anything this test is about.
  check(waitFor([&]() { return !camera->inAttempt(); }, 6000),
        "the first camera's attempt has ended before the next phase");

  auto second = std::make_shared<BlindConfirmCamera>(1500, 1500);
  control.addActive(second);
  check(control.getTargetCount() == 1, "the second camera is accepted");
  control.connectAll(true);
  // The drain from the previous phase gates STATE_CONNECT until its zombie
  // reaps, which needs the reclaim deadline as well as the attempt ending, so
  // this bound is the sum of both rather than one attempt.
  check(waitFor([&]() { return second->inAttempt(); }, 10000),
        "the second attempt reaches the blind window");
  control.disconnect(50);
  check(control.getTargetCount() == 0, "the second target drained");
  second->clearConnectCancel();

  control.disconnect(50);
  const bool cancelled = waitFor([&]() { return second->cancelObserved(); }, 2500);
  check(cancelled, "a teardown cancels a drained attempt whose token was cleared");

  control.disconnect();
  check(waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, 3000),
        "control returns to idle after the third phase");

  // Phase 4. A connect cycle with no cameras must fail with a reason rather than
  // publishing a session that contains nothing. Reachable independently of the
  // withdrawn refusal: both entry points call connectAll() unconditionally, so a
  // failed xTaskCreate in addActive() lands here too.
  check(control.getTargetCount() == 0, "no targets before the empty connect");
  control.connectAll(false);
  check(waitFor([&]() { return control.getState() == Control::STATE_CONNECT_FAILED; }, 3000),
        "a connect with no cameras fails instead of going active");
  check(control.getState() != Control::STATE_ACTIVE, "an empty session is never active");

  g_ProbeRun = false;
  probe.join();
  check(!g_PhantomActive.load(), "control never published active with no targets, whole run");

  if (g_Failures != 0) {
    std::cout << "control-zombie-cancel: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-zombie-cancel: PASS\n";
  return 0;
}
