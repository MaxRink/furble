// Host regression test for the furble-initiated fast reconnect (task #54).
//
// This compiles the real src/FurbleControl.cpp against MockNimBLE, the real
// lib/furble Camera and the Fujifilm sources, and the host FreeRTOS shim under
// tests/host/control. It drives the production control task through the two
// reconnect paths and pins their timing.
//
// The behaviour it locks in: the #122 first-retry backoff (FIRST_RETRY_MS,
// 2.5 s) exists to let a stale session on the camera expire before reconnecting.
// When furble itself initiated the prior disconnect there is no stale peer
// session, so that wait is avoidable latency. A furble-initiated fresh connect
// therefore skips the first-retry wait; a peer-initiated mid-session drop keeps
// it.
//
// The boot test runs first and pins the fail-safe: with no clean-restart
// marker consumed (the host Settings shim returns false, the same as an
// unreadable marker or a true first boot) the very first connect keeps the
// patient peer backoff. Test A drives a reconnect after a completed
// interactive disconnect and asserts the retry is immediate (well under the
// 2.5 s wait). Test B drives a mid-session peer drop whose first reconnect
// attempt fails and asserts the 2.5 s first-retry backoff is still honoured.
//
// Mutation checks: revert m_NextConnectOrigin's default to FURBLE and the boot
// test's elapsed floor fails because the first connect turns fast. Revert
// delayMs() to ignore the queued origin (always FIRST_RETRY_MS at attempt 0)
// and Test A's fast bound fails because the reconnect waits the full 2.5 s.
// The handshake-reset test similarly catches any implementation that trusts a
// caller-supplied clean origin.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"

const char *LOG_TAG = "furble-host";

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// 0x08 is the connection supervision timeout, the reason a powered-off or out of
// range camera drops with. This is the peer-initiated drop the backoff guards.
constexpr int REASON_SUPERVISION_TIMEOUT = 0x08;

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

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

void ensureControlTask() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  std::thread(control_task, &Furble::Control::getInstance()).detach();
}

using Furble::Control;

// Boot fail-safe: the host Settings shim's consumeCleanRestart() returns
// false, the same result as a marker lost to power failure, an NVS error, or
// a true first boot. The very first connect of the process must therefore
// keep the patient peer backoff. This test must run first: it consumes the
// one boot token before any disconnect() mints a fresh FURBLE token.
//
// Mutation check: revert Control::m_NextConnectOrigin's default to FURBLE and
// the elapsed floor below fails because the first retry turns immediate.
bool testBootWithoutMarkerKeepsBackoff() {
  std::cout << "test: a boot without a consumed clean-restart marker keeps the backoff\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Fail the very first connect attempt so the first-retry path runs at
  // attempt 0 with the boot origin token.
  NimBLEDevice::setConnectFailCount(1);

  const auto start = std::chrono::steady_clock::now();
  control.connectAll(true);

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 8000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(connected, "the boot connect eventually completes");
  // No marker was consumed, so the first retry must wait roughly
  // FIRST_RETRY_MS (2500 ms). A 2000 ms floor proves the wait happened while
  // tolerating scheduling jitter.
  check(elapsed >= 2000, "a boot without the clean-restart marker keeps the first-retry backoff");
  std::cout << "  unmarked boot connect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Test A: a reconnect after a completed interactive disconnect. The teardown
// mints the FURBLE token, so when the next connect's first attempt fails the
// retry at attempt 0 must be immediate and the camera reaches active well
// inside the 2.5 s stale-session wait.
bool testFurbleInitiatedReconnectIsFast() {
  std::cout << "test: a furble-initiated reconnect skips the 2.5 s first-retry wait\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Establish the session, then tear it down through the interactive
  // disconnect. The completed teardown is what mints the clean FURBLE token.
  control.connectAll(true);
  if (!check(waitFor([&] { return control.getConnectedTargetCount() == 1; }, 4000),
             "the initial connect reaches active")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return false;
  }
  control.disconnect();
  if (!check(waitFor([&] { return control.getTargetCount() == 0; }, 2000),
             "the interactive disconnect completes")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Reconnect with a failing first attempt so the first-retry path runs at
  // attempt 0. AUTO consumes the token carried by the interactive disconnect.
  control.addActive(camera);
  NimBLEDevice::setConnectFailCount(1);

  const auto start = std::chrono::steady_clock::now();
  control.connectAll(true);

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 4000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(connected, "the furble-initiated reconnect completes");
  // The old stale-session wait was 2500 ms; the fast path is immediate. A bound
  // well under 2500 ms fails if the wait is not skipped, and is loose enough to
  // absorb the control task's 50 ms tick, the zombie drain and the handshake.
  check(elapsed < 1500,
        "the furble-initiated reconnect does not wait FIRST_RETRY_MS before retrying");
  std::cout << "  furble-initiated reconnect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// A connect request received while the task already sits in STATE_CONNECT
// must re-arm the running cycle instead of being silently dropped, and the
// re-arm keeps the request's furble origin when the interrupted cycle is
// itself furble-originated.
bool testConnectDuringConnectRearms() {
  std::cout << "test: a connect request during STATE_CONNECT re-arms and keeps its origin\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();
  control.addActive(camera);

  // Three failures: the first burns the immediate furble retry, the second
  // parks the cycle in the 5 s BASE_MS wait, and the third falls to the
  // re-armed cycle whose immediate furble retry then succeeds.
  NimBLEDevice::setConnectFailCount(3);

  const auto start = std::chrono::steady_clock::now();
  control.connectAll(true, Control::reconnect_origin_t::FURBLE);
  // Queue a second furble request during the BASE_MS wait. The control task
  // receives it in STATE_CONNECT once the wait ends.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  control.connectAll(true, Control::reconnect_origin_t::FURBLE);

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 9000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(connected, "the re-armed connect completes");
  // With the re-arm the third attempt runs right after the 5 s wait and its
  // immediate furble retry succeeds at about 5.2 s. A dropped request keeps
  // the old cycle's counter and waits another BASE_MS (about 10.2 s), and a
  // re-arm that lost the furble origin adds the 2.5 s peer wait (about
  // 7.7 s). The bound rejects both.
  check(elapsed < 6500, "the queued connect re-arms the cycle and keeps the furble origin");
  std::cout << "  re-armed connect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// Test B: a mid-session peer-initiated drop. The initial connect succeeds and
// reaches active, clearing the fresh-connect flag. A supervision-timeout drop
// then severs the link, and the first reconnect attempt fails. Because the drop
// was peer-initiated (the camera may still hold the previous session), the
// first-retry wait must still be honoured.
bool testPeerInitiatedDropKeepsBackoff() {
  std::cout << "test: a peer-initiated drop still waits the 2.5 s first-retry backoff\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();

  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    NimBLEDevice::resetMock();
    return false;
  }

  // Fresh connect succeeds on the first attempt and reaches active, so the
  // fresh-connect flag is cleared. Infinite reconnect stays on for the drop.
  control.connectAll(true);
  if (!check(waitFor([&] { return control.getConnectedTargetCount() == 1; }, 4000),
             "the initial connect reaches active")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return false;
  }

  // Arm a single reconnect failure, then drop the live link with onDisconnect,
  // exactly as a supervision timeout does. The control task sees the drop, enters
  // reconnect, its first attempt fails, and it must wait the first-retry backoff.
  NimBLEDevice::setConnectFailCount(1);
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the camera created a client")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  client->mockDropLink(REASON_SUPERVISION_TIMEOUT, /*fire_callback=*/true);

  const bool reconnected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 8000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(reconnected, "the peer-initiated drop eventually reconnects");
  // The stale-session wait must still apply here: the reconnect cannot complete
  // before roughly FIRST_RETRY_MS (2500 ms). A 2000 ms floor proves the wait
  // happened while tolerating scheduling jitter.
  check(elapsed >= 2000,
        "the peer-initiated drop keeps the first-retry backoff before reconnecting");
  std::cout << "  peer-drop reconnect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// A peer reset during the first handshake is still peer-originated even though
// the public connect request started the cycle. The first retry must therefore
// keep the stale-session wait.
bool testPeerResetDuringHandshakeKeepsBackoff() {
  std::cout << "test: a peer reset during handshake keeps the first-retry backoff\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  peer.dropLinkDuringConnect(Furble::Host::FujifilmVirtualCamera::pairServiceUUID(),
                             Furble::Host::FujifilmVirtualCamera::identifierCharacteristicUUID());
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();
  control.addActive(camera);

  const auto start = std::chrono::steady_clock::now();
  // Deliberately present a clean-origin token. The virtual peer resets the
  // link during the real handshake; Camera's callback must override it to
  // PEER before the retry is scheduled.
  control.connectAll(true, Control::reconnect_origin_t::FURBLE);
  // Remove the fault after the first attempt has had time to reach the
  // identify write. The retry itself must still honor the peer wait.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  peer.clearFaults();

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 8000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(connected, "the peer-reset connect eventually recovers");
  check(elapsed >= 2000, "a peer reset during handshake does not take the furble fast path");
  std::cout << "  peer-reset reconnect completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

// A manual connect request arriving while the automatic peer recovery is
// waiting must not mutate that running cycle's origin or retry configuration.
bool testManualConnectDuringPeerRecoveryKeepsBackoff() {
  std::cout << "test: manual connect during peer recovery keeps its origin\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  ensureControlTask();

  Furble::Host::FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  auto &control = Control::getInstance();
  control.addActive(camera);
  control.connectAll(true, Control::reconnect_origin_t::PEER);
  if (!check(waitFor([&] { return control.getConnectedTargetCount() == 1; }, 4000),
             "the peer-recovery test starts active")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return false;
  }

  NimBLEDevice::setConnectFailCount(1);
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the peer-recovery test created a client")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  client->mockDropLink(0x08, /*fire_callback=*/true);
  // Queue concurrent requests while the automatic peer reconnect is in its
  // first failed attempt. Neither may rewrite the active cycle's origin.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::thread fastRequest(
      [&control] { control.connectAll(false, Control::reconnect_origin_t::FURBLE); });
  std::thread patientRequest(
      [&control] { control.connectAll(true, Control::reconnect_origin_t::PEER); });
  fastRequest.join();
  patientRequest.join();

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 8000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  check(connected, "peer recovery survives a concurrent manual connect request");
  check(elapsed >= 2000, "a concurrent manual connect request cannot shorten peer recovery");
  std::cout << "  concurrent peer recovery completed in " << elapsed << " ms\n";

  control.disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  // Order matters: the boot test consumes the process's one boot origin token
  // and must run before any disconnect() mints a fresh FURBLE token.
  testBootWithoutMarkerKeepsBackoff();
  testFurbleInitiatedReconnectIsFast();
  testConnectDuringConnectRearms();
  testPeerInitiatedDropKeepsBackoff();
  testPeerResetDuringHandshakeKeepsBackoff();
  testManualConnectDuringPeerRecoveryKeepsBackoff();

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "reconnect initiator harness: PASS\n";
  } else {
    std::cout << "reconnect initiator harness: FAIL (" << g_Failures << " checks)\n";
  }

  // The control task runs for the whole process and is never joined. Flush and
  // exit immediately so its detached thread is not torn down against destroyed
  // singletons on return from main.
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(status);
}
