// Bench reproduction: the Fujifilm Secure stale-bond stall wedges the session.
//
// Hardware, 2026-09-04, master 8bdc52e4 with PR 274 in. Twenty console cycles of
// `connect 0` then `disconnect` at 2 s, 4 s and 6 s left Control at
// `state disconnecting`, `connect_in_progress true`, `connecting none`, zombies
// climbing with no new commands, a fresh `connect 0` refused with "already
// connecting, ignoring duplicate connect", and `task.control` at 0.0 percent
// CPU, for more than two minutes until a reboot.
//
// The mechanism, which this harness reproduces exactly:
//
// 1. FujifilmSecure::_connect() calls m_Client->secureConnection(). That is a
//    blocking NimBLE call with its own internal timeout, not a poll loop. When
//    the camera has deleted its side of the bond it blocks for the full pairing
//    timeout, and Camera::connect() holds Camera::m_Mutex for all of it. The
//    plan 148 cancel token cannot reach it: the polls in FujifilmSecure sit in
//    the scan phase before the call, not inside it.
// 2. m_ConnectInProgress therefore stays true, so targetTasksStopped() and
//    disconnectComplete() are both false and every interactive disconnect burns
//    its whole cap and drains its targets into m_ZombieTargets.
// 3. reapZombieTargets() frees a drained target only once its task publishes
//    m_Stopped. That task is blocked on the same Camera::m_Mutex, so it never
//    does. Zombies accumulate and teardownDraining() gates STATE_CONNECT.
//
// The peer stalls secureConnection() rather than returning false, which is the
// whole point: a returned failure unwinds, a block does not. The stall is a
// condition variable released by the peer's own disconnect(), not a sleep,
// because the block is only half the behaviour: NimBLE returns from a parked
// secureConnection() when the link is terminated under it, and that wake is
// exactly what abortBlockingConnect() buys. Against a plain sleep every
// disconnect here would pass by outwaiting the stall and the harness would
// prove nothing about the fix.
//
// PASSES on PR 245, which lands abortBlockingConnect().
//
// What master actually does, measured rather than assumed: the fresh connect at
// the end DOES reach active there, and the only check master fails is the
// abort-provenance one, `aborts == cycles`. Every disconnect on master gets
// there by outwaiting the stall instead of ending it. An earlier version of
// this header claimed master left the session wedged at
// `state=STATE_CONNECTING targets=1 connected=0`; that was the harness dying in
// the saved-reconnect scan wait before it ever reached the handshake, not the
// wedge, and the Advertiser below is what fixed it.
//
// So `aborts == cycles` is the load-bearing assertion and the only one that
// separates this branch from master. `check(slowest < STALL_MS)` below has no
// teeth and is kept only as a diagnostic: `Control::disconnect(300)` returns on
// its own cap well inside the 3000 ms stall, so it passes under the mutation
// too. Read a failure there as a timing surprise worth investigating, not as
// the regression.
//
// Mutation, re-run on this branch: delete the terminate from
// Camera::abortBlockingConnect() and the run fails on
// "every disconnect ended the handshake by terminating the link".
//
// This is PR 245's gate, not plan 170's. Plan 170 cancels the drain set and the
// attempt in flight, which fixes every wait that polls the cancel token. This
// one polls nothing: secureConnection() is a bare blocking call and only the
// link terminate wakes it, which is what abortBlockingConnect() issues. Setting
// the token against this wait is a no-op, so no amount of cancel plumbing in
// Control can close it.
//
// It lives on its own branch rather than in PR 272 because it cannot pass in
// that PR's CI. Enable it as a ctest with PR 245.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "CameraList.h"
#include "Device.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "Scan.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

namespace Furble {
// Scan.cpp calls into the saved-camera list on every advertisement. This
// harness drives Control directly and never loads a list, so the same stub the
// other Secure host tests use keeps the link closed without pulling in
// Preferences.
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  return false;
}
}  // namespace Furble

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

bool waitFor(const std::function<bool()> &predicate, uint32_t timeout_ms) {
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

/**
 * Keep the saved-reconnect scan fed.
 *
 * Required, not decoration. Camera::connect() sets m_Paired on the first
 * success, and FujifilmSecure::_connect() scans for the advertisement before
 * every attempt once that is set. Without a radio answering, every cycle after
 * the baseline connect dies in the scan wait and never reaches
 * secureConnection() at all: measured, the stall was entered 0 times out of 9.
 * A harness in that state reports the wedge whatever the connect path does,
 * because nothing ever blocks. On the bench the camera advertises continuously;
 * this thread is that radio.
 */
class Advertiser {
 public:
  explicit Advertiser(const NimBLEAdvertisedDevice &advertisement)
      : m_Advertisement(advertisement), m_Thread([this]() { run(); }) {}

  ~Advertiser() {
    m_Stop = true;
    m_Thread.join();
  }

 private:
  void run() {
    while (!m_Stop.load()) {
      NimBLEDevice::getScan()->emitResult(&m_Advertisement);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  NimBLEAdvertisedDevice m_Advertisement;
  std::atomic<bool> m_Stop {false};
  std::thread m_Thread;
};

// The bench pressed disconnect at 2 s, 4 s and 6 s into a connect whose stall is
// 30 s. Scaled by 10 so the run is seconds: the stall outlives every disconnect
// in the cycle, which is the property that matters.
constexpr uint32_t STALL_MS = 3000;
constexpr uint32_t DISCONNECT_AT_MS[] = {200, 400, 600};
constexpr uint32_t CYCLES = 3;

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

  FujifilmVirtualCamera::Config config;
  config.secure = true;
  FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmSecure>(&advertisement);
  Advertiser advertiser(advertisement);

  // Baseline first: prove the harness can connect this camera at all, so a
  // later failure is the wedge and not a broken fixture.
  control.addActive(camera);
  control.connectAll(false);
  check(waitFor([&]() { return control.getState() == Control::STATE_ACTIVE; }, 20000),
        "baseline: the Secure camera connects before any stall");
  control.disconnect();
  waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, 5000);

  peer.setSecureConnectionStallMs(STALL_MS);

  // The bench had infinite reconnect on, which is what kept the session alive
  // across the cycles instead of failing out after two attempts.
  uint32_t aborts = 0;
  uint32_t slowest = 0;
  bool reachedEveryCycle = true;
  for (uint32_t cycle = 0; cycle < CYCLES; cycle++) {
    for (uint32_t at : DISCONNECT_AT_MS) {
      const uint32_t entriesBefore = peer.secureStallEntries();
      control.addActive(camera);
      control.connectAll(true);

      // Wait for the attempt to actually reach the blocking handshake, rather
      // than sleeping a fixed time and assuming it got there. The connect has
      // to scan, link and run the Secure handshake up to secureConnection()
      // first, and on a loaded host that took longer than the shortest offset,
      // so a cancel landed before the stall had been entered and the run
      // proved nothing about ending a blocking call. Measured on this branch,
      // that lost 1 full-suite run in 6 under load.
      if (!waitFor([&]() { return peer.secureStallEntries() > entriesBefore; }, STALL_MS * 3)) {
        reachedEveryCycle = false;
        control.disconnect();
        waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, STALL_MS * 3);
        continue;
      }

      // Now land the cancel at varying depths inside the stall, which is what
      // the bench varied: 2 s, 4 s and 6 s into a connect that was parked.
      std::this_thread::sleep_for(std::chrono::milliseconds(at));
      const uint32_t issued = nowMs();
      control.disconnect(300);
      // Back to idle is the observable end of the attempt. Time it: a
      // disconnect that ends the parked handshake returns in well under the
      // stall, one that merely outwaits it cannot.
      waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, STALL_MS * 3);
      const uint32_t elapsed = nowMs() - issued;
      slowest = (elapsed > slowest) ? elapsed : slowest;
      if (peer.secureStallWasAborted()) {
        aborts++;
      }
    }
  }

  const uint32_t cycles = CYCLES * (sizeof(DISCONNECT_AT_MS) / sizeof(DISCONNECT_AT_MS[0]));
  check(reachedEveryCycle && (peer.secureStallEntries() >= cycles),
        "every cycle reached the blocking handshake");
  check(aborts == cycles, "every disconnect ended the handshake by terminating the link");
  check(slowest < STALL_MS, "and none of them had to outwait the stall to get there");
  if (slowest >= STALL_MS) {
    std::cerr << "  (slowest disconnect took " << slowest << " ms against a " << STALL_MS
              << " ms stall)\n";
  }

  // Let the last stall expire with room to spare, then require the session to be
  // usable again. The zombie count itself is only reachable through
  // getDebugState(), which is FURBLE_CONSOLE only, and defining that here would
  // pull eleven otherwise unbuilt blocks of Scan.cpp into the coverage union as
  // uncovered lines and fail that file's floor. So the assertion is the
  // user-visible consequence instead, which is also what the bench measured: a
  // drain that never reaps keeps teardownDraining() true, and that gates
  // STATE_CONNECT, so a fresh connect can never reach active. The per-cycle
  // provenance and timing checks above are what prove each attempt was ended
  // rather than outwaited.
  control.disconnect();
  waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, STALL_MS * 4);

  peer.setSecureConnectionStallMs(0);
  control.addActive(camera);
  check(control.getTargetCount() == 1, "the camera can be added again after the wedge");
  control.connectAll(false);
  check(waitFor([&]() { return control.getState() == Control::STATE_ACTIVE; }, 20000),
        "a fresh connect reaches active after the stall cycles");
  check(control.getConnectedTargetCount() == 1, "the fresh connect is really connected");

  control.disconnect();
  waitFor([&]() { return control.getState() == Control::STATE_IDLE; }, 5000);

  if (g_Failures != 0) {
    std::cout << "control-secure-stall: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-secure-stall: PASS\n";
  return 0;
}
