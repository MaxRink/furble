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
// whole point: a returned failure unwinds, a block does not.
//
// EXPECTED TO FAIL until PR 245 lands abortBlockingConnect().
//
// Verified failing identically on master 8bdc52e4 and on the plan 170 branch
// a769449: `end state=STATE_CONNECTING targets=1 connected=0`, with the fresh
// connect never reaching active. The baseline check below connects the same
// camera with no stall, so a failure here is the wedge and not a broken
// fixture.
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
  for (uint32_t cycle = 0; cycle < CYCLES; cycle++) {
    for (uint32_t at : DISCONNECT_AT_MS) {
      control.addActive(camera);
      control.connectAll(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(at));
      control.disconnect(300);
    }
  }

  // Let the last stall expire with room to spare, then require the session to be
  // usable again. The zombie count itself is only reachable through
  // getDebugState(), which is FURBLE_CONSOLE only, so the assertion is the
  // user-visible consequence instead, which is also what the bench measured: a
  // drain that never reaps keeps teardownDraining() true, and that gates
  // STATE_CONNECT, so a fresh connect can never reach active.
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
