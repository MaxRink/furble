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
// Test A drives a fresh, furble-initiated connect whose first attempt fails and
// asserts the retry is immediate (well under the 2.5 s wait). Test B drives a
// mid-session peer drop whose first reconnect attempt fails and asserts the
// 2.5 s first-retry backoff is still honoured.
//
// Mutation check: revert delayMs() to ignore the furbleInitiated flag (always
// FIRST_RETRY_MS at attempt 0), or drop the m_FreshConnect wiring in
// connectAll(), and Test A's fast bound fails because the fresh reconnect waits
// the full 2.5 s. That is the test's tooth.

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

// Test A: a fresh, furble-initiated connect. The first connect attempt fails, so
// the control task enters the first-retry path at attempt 0. Because the connect
// was furble-initiated (no stale peer session), the retry must be immediate, so
// the camera reaches active well inside the 2.5 s stale-session wait.
bool testFurbleInitiatedReconnectIsFast() {
  std::cout << "test: a furble-initiated fresh reconnect skips the 2.5 s first-retry wait\n";
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

  // Fail the very first connect attempt so the first-retry path is exercised at
  // attempt 0. The next attempt then succeeds.
  NimBLEDevice::setConnectFailCount(1);

  const auto start = std::chrono::steady_clock::now();
  // Infinite reconnect on: this is the only mode where the first-retry backoff
  // applies. connectAll(bool) marks the cycle fresh (furble-initiated).
  control.connectAll(true);

  const bool connected = waitFor([&] { return control.getConnectedTargetCount() == 1; }, 4000);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  check(connected, "the fresh furble-initiated reconnect completes");
  // The old stale-session wait was 2500 ms; the fast path is immediate. A bound
  // well under 2500 ms fails if the wait is not skipped, and is loose enough to
  // absorb the control task's 50 ms tick and the connect handshake.
  check(elapsed < 1500,
        "the fresh furble-initiated reconnect does not wait FIRST_RETRY_MS before retrying");
  std::cout << "  fresh reconnect completed in " << elapsed << " ms\n";

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

}  // namespace

int main() {
  testFurbleInitiatedReconnectIsFast();
  testPeerInitiatedDropKeepsBackoff();

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
