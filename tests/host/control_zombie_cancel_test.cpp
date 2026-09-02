// Regression for issue 271: a draining target whose connect attempt is still in
// flight.
//
// The reachable route, on hardware, starts with a vendor wait that does not
// poll the plan 148 cancel token. CanonEOSSmart::_connect() waited 60 s for the
// user to confirm pairing, twice Control::DISCONNECT_WAIT_MAX_MS, and never
// polled; DJIOsmo's protocol handshake waited the full 30 s the same way. A
// user disconnect during one of those could not abort the attempt, so the
// teardown burned its whole cap, broke out silently, and drained the target
// with the attempt still running. Control::addActive() deduplicates against
// m_Targets only, so tapping connect again handed that same camera a fresh
// target, and Control::connectAll(bool) then cleared the very cancel token the
// teardown had set. The attempt became uncancellable for the rest of its vendor
// timeout, its target task stayed blocked on Camera::m_Mutex so the drained
// target never published m_Stopped, and teardownDraining() held the connect
// gate shut for up to a minute.
//
// The camera here models that shape directly rather than through a vendor peer:
// a blind window inside _connect() that ignores the cancel token, then a polling
// window that honours it, which is what CanonEOSSmart looks like before and
// after its one-line fix. Every bound is milliseconds so the pre-fix behaviour
// fails deterministically without waiting out a real vendor timeout.
//
// Phase 1 proves the interactive path honours timeout_ms. Pre-fix it ignored
// the parameter and always waited DISCONNECT_WAIT_MAX_MS, so a camera in its
// blind window pinned the caller for the whole blind wait.
// Phase 2 proves addActive() refuses a camera that is still draining, instead
// of handing it a fresh target whose connect cycle clears the cancel token.
// Phase 3 proves the teardown is self-sufficient: with the token explicitly
// cleared, exactly what a pre-fix connect cycle did, a disconnect still cancels
// the attempt because it now walks the drain set and the attempt in flight.

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

  // Phase 2. Tapping connect again must not hand the draining camera a fresh
  // target: that target's connect cycle would clear the cancel token the
  // teardown just set.
  control.addActive(camera);
  check(control.getTargetCount() == 0, "a draining camera is refused a fresh target");

  // The refusal re-arms the cancel, so the attempt aborts as soon as it reaches
  // its polling window, the drained target reaps, and the camera is accepted
  // again. Pre-fix the attempt ran to the end of its vendor timeout.
  const bool reAccepted = waitFor(
      [&]() {
        control.addActive(camera);
        return control.getTargetCount() == 1;
      },
      4000);
  check(reAccepted, "the drain clears and the camera is accepted once it has");
  check(camera->cancelObserved(), "the attempt observed the cancel rather than timing out");

  control.disconnect();
  check(waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, 3000),
        "control returns to idle");

  // Phase 3. The teardown must not depend on a token an earlier call happened to
  // set. Drain the camera again, clear the token exactly as a pre-fix connect
  // cycle did, and require the next disconnect to cancel the attempt anyway.
  // Let the first camera's session go fully quiet first. A drained target is
  // only reaped once its attempt ends, and teardownDraining() gates
  // STATE_CONNECT until it is, so starting the next phase early would time out
  // on the gate rather than on anything this test is about.
  check(waitFor([&]() { return !camera->inAttempt(); }, 4000),
        "the first camera's attempt has ended before the next phase");

  auto second = std::make_shared<BlindConfirmCamera>(1500, 1500);
  const bool secondAccepted = waitFor(
      [&]() {
        control.addActive(second);
        return control.getTargetCount() == 1;
      },
      4000);
  check(secondAccepted, "the second camera is accepted");
  control.connectAll(true);
  check(waitFor([&]() { return second->inAttempt(); }, 6000),
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

  if (g_Failures != 0) {
    std::cout << "control-zombie-cancel: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-zombie-cancel: PASS\n";
  return 0;
}
