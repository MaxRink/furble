// End-to-end harness for the REAL Furble::Control connect/disconnect/reconnect
// state machine.
//
// Unlike the sim (real UI over a fake Control) and the other host tests (real
// Camera driven directly, no Control), this harness compiles the real
// src/FurbleControl.cpp and runs it on a real FreeRTOS-style task against the
// real lib/furble Camera lifecycle and the shared MockNimBLE + Fujifilm virtual
// camera peer. The connect flow is driven exactly as the device drives it:
// Control::addActive() then Control::connectAll(), disconnect via
// Control::disconnect(). Scenarios inject the same BLE faults the host fault
// tests use (drop link, stale session, connect-fail-count, pool exhaustion) and
// assert the real control state, target counts and, crucially, timing bounds so
// the ~30 s interactive-disconnect freeze class is caught here rather than only
// on hardware.

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

const char *LOG_TAG = "furble-control-e2e";

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

[[maybe_unused]] const char *stateName(Control::state_t state) {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "connect_failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnecting";
  }
  return "?";
}

// Poll the control state until it matches (or times out). Returns true on match.
bool waitForState(Control::state_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t deadline = nowMs() + timeout_ms;
  while (nowMs() < deadline) {
    if (control.getState() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() == want;
}

// Count writes the peer has seen to the shutter characteristic. A shutter press
// or release is exactly such a write, so this is how many shutter commands
// actually reached the camera since the last clearEvents().
size_t shutterWriteCount(const FujifilmVirtualCamera &peer) {
  size_t count = 0;
  for (const auto &write : peer.writes()) {
    if (write.characteristic == FujifilmVirtualCamera::shutterCharacteristicUUID().toString()) {
      count++;
    }
  }
  return count;
}

// Poll until the control state is anything other than `avoid` (or times out).
bool waitForNotState(Control::state_t avoid, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t deadline = nowMs() + timeout_ms;
  while (nowMs() < deadline) {
    if (control.getState() != avoid) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() != avoid;
}

bool waitForConnectedCount(size_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t deadline = nowMs() + timeout_ms;
  while (nowMs() < deadline) {
    if (control.getConnectedTargetCount() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getConnectedTargetCount() == want;
}

bool waitForShutterWrites(const FujifilmVirtualCamera &peer, size_t want, uint32_t timeout_ms) {
  const uint32_t deadline = nowMs() + timeout_ms;
  while (nowMs() < deadline) {
    if (shutterWriteCount(peer) >= want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return shutterWriteCount(peer) >= want;
}

// Build a real Fujifilm camera bound to a virtual peer and register the peer.
std::shared_ptr<Furble::FujifilmBasic> makeCamera(FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// Bring the singleton control back to a clean idle baseline between scenarios.
void resetControl() {
  auto &control = Control::getInstance();
  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
}

void startControlTask() {
  static std::atomic<bool> started {false};
  bool expected = false;
  if (started.compare_exchange_strong(expected, true)) {
    auto &control = Control::getInstance();
    xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);
  }
}

void freshEnvironment() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  // Default settings for the connection: keep the device awake while active so
  // the sleep-lock balance can be asserted, no adaptive power, no backoff, no
  // connection saver.
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);
}

// Interactive disconnect of a camera whose link was silently dropped. On the
// device the link supervision timeout (bounded by the m_IdleTimeout fix) later
// fires onDisconnect and unblocks the teardown. The mock has no supervision
// timer, so a helper thread stands in for it and fires the detected drop shortly
// after, so the wait is bounded the way hardware bounds it.
bool boundedDeadDisconnect(NimBLEClient *client) {
  std::thread supervision([client]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (client != nullptr) {
      client->mockDropLink(0x08, /*fire_callback=*/true);
    }
  });
  const bool completed = Control::getInstance().disconnect();
  supervision.join();
  return completed;
}

// --- Scenarios -------------------------------------------------------------

// A fresh connect reaches ACTIVE quickly, holds the sleep lock, and the
// interactive disconnect completes promptly and leaves no leaked client.
bool scenarioFreshConnect() {
  freshEnvironment();
  auto &control = Control::getInstance();
  auto &power = Furble::Power::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);
  check(control.getTargetCount() == 1, "one target after addActive");

  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "reaches active within 5 s");
  check(control.getConnectedTargetCount() == 1, "one connected target");
  check(control.allConnected(), "all connected");
  check(power.getCount(Furble::Power::LockType::NO_LIGHT_SLEEP) >= 1,
        "sleep lock held while active");

  const uint32_t start = nowMs();
  const bool completed = control.disconnect();
  const uint32_t elapsed = nowMs() - start;
  check(completed, "interactive disconnect completes");
  check(elapsed < 3000, "disconnect returns within 3 s (no freeze)");
  check(waitForState(Control::STATE_IDLE, 2000), "returns to idle");
  check(control.getTargetCount() == 0, "no targets after disconnect");
  // The camera object (still referenced by this scenario) keeps its one client
  // for reuse; the leak class is more than one, guarded in client-pool-exhaustion.
  check(NimBLEDevice::liveClientCount() <= 1, "no client leak after disconnect");
  check(power.getCount(Furble::Power::LockType::NO_LIGHT_SLEEP) == 0, "sleep lock released");
  return g_Failures == 0;
}

// The dead-camera interactive disconnect must not freeze. The camera drops its
// link with no disconnect callback (powered off), leaving a stale connected
// flag, and the user taps disconnect. This is the ~30 s freeze class: assert the
// disconnect still returns promptly while a background reader keeps observing
// state (proving the state machine never wedges).
bool scenarioDeadCameraDisconnectNoFreeze() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "reaches active");

  // Background observer: mirrors the UI render reading control state. Records
  // the longest gap between successive reads to prove nothing stalls it.
  std::atomic<bool> stop {false};
  std::atomic<uint32_t> maxGapMs {0};
  std::thread observer([&]() {
    uint32_t last = nowMs();
    while (!stop.load()) {
      (void)control.getState();
      (void)control.getConnectedTargetCount();
      const uint32_t t = nowMs();
      const uint32_t gap = t - last;
      last = t;
      uint32_t prev = maxGapMs.load();
      while (gap > prev && !maxGapMs.compare_exchange_weak(prev, gap)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });

  // Camera powered off: the link is severed with no onDisconnect callback, so
  // the camera keeps a stale connected flag. On the device the link supervision
  // timeout (bounded to ~7 s by the m_IdleTimeout fix) later fires onDisconnect
  // and unblocks the interactive teardown. The mock has no supervision timer, so
  // a background thread stands in for it and fires the detected drop shortly
  // after the user taps disconnect. The assertion is that the interactive
  // disconnect returns promptly once that fires, never wedging the UI task.
  NimBLEClient *client = NimBLEDevice::lastClient();
  check(client != nullptr, "camera created a client");
  if (client != nullptr) {
    client->mockDropLink(0x08, /*fire_callback=*/false);
  }
  check(camera->isConnected(), "stale connected flag reads true after the silent drop");

  std::thread supervision([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    if (client != nullptr) {
      client->mockDropLink(0x08, /*fire_callback=*/true);
    }
  });

  const uint32_t start = nowMs();
  const bool completed = control.disconnect();
  const uint32_t elapsed = nowMs() - start;

  supervision.join();
  stop.store(true);
  observer.join();

  check(completed, "dead-camera disconnect completes once the drop is detected");
  check(elapsed < 3000, "dead-camera disconnect returns within 3 s (no ~30 s freeze)");
  check(maxGapMs.load() < 1000, "state observer never stalled during disconnect");
  check(waitForState(Control::STATE_IDLE, 2000), "returns to idle");
  check(control.getTargetCount() == 0, "targets cleared");
  return g_Failures == 0;
}

// After a dead-camera disconnect, a fresh connect to a live camera must
// complete in the same bounded window, not inherit any stuck state.
bool scenarioConnectAfterDeadDisconnect() {
  freshEnvironment();
  auto &control = Control::getInstance();

  {
    FujifilmVirtualCamera peer;
    auto camera = makeCamera(peer);
    control.addActive(camera);
    control.connectAll(false);
    check(waitForState(Control::STATE_ACTIVE, 5000), "first connect active");
    NimBLEClient *client = NimBLEDevice::lastClient();
    if (client != nullptr) {
      client->mockDropLink(0x08, /*fire_callback=*/false);
    }
    boundedDeadDisconnect(client);
    check(waitForState(Control::STATE_IDLE, 3000), "dead disconnect back to idle");
  }

  // Fresh environment for the live camera (drops the connect-fail state).
  NimBLEDevice::resetMock();
  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);

  const uint32_t start = nowMs();
  control.connectAll(false);
  const bool active = waitForState(Control::STATE_ACTIVE, 3000);
  const uint32_t elapsed = nowMs() - start;
  check(active, "reconnect after dead disconnect reaches active");
  check(elapsed < 2500, "connect after dead disconnect completes within ~2 s");
  check(control.getConnectedTargetCount() == 1, "connected after dead disconnect");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// A stale-session reconnect (the camera still holds the previous CCCD session)
// must complete, bounded, and reach a connected active state.
bool scenarioStaleSessionReconnect() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "first connect active");
  control.disconnect();
  check(waitForState(Control::STATE_IDLE, 3000), "clean disconnect to idle");

  // Fast reconnect against a stale subscribe session.
  peer.setStaleSubscribeSession(true);
  peer.clearEvents();
  control.addActive(camera);

  const uint32_t start = nowMs();
  control.connectAll(false);
  const bool active = waitForState(Control::STATE_ACTIVE, 5000);
  const uint32_t elapsed = nowMs() - start;
  check(active, "stale-session reconnect reaches active");
  check(elapsed < 5000, "stale-session reconnect is bounded");
  check(control.getConnectedTargetCount() == 1, "connected after stale reconnect");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// BUG C: a Camera carried on the persistent list may hold a stale connected
// flag from a prior session whose onDisconnect never fired. A fresh connect must
// clear it (Control::addActive -> resetConnectionState) and do real BLE work,
// not short-circuit to connected.
bool scenarioFalseConnectedGuard() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "first connect active");

  // Sever the link with no callback: the shared camera keeps a stale connected
  // flag, exactly the persistent-list state BUG C describes.
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (client != nullptr) {
    client->mockDropLink(0x08, /*fire_callback=*/false);
  }
  check(camera->isConnected(), "stale connected flag reads true before reconnect");
  // Force-complete the teardown (the restart path) so the stale connected flag
  // survives on the shared camera object without waiting on a supervision timer
  // the mock does not model. This preserves exactly the BUG C precondition.
  control.disconnect(300, /*forRestart=*/true);
  waitForState(Control::STATE_IDLE, 3000);
  check(camera->isConnected(), "stale connected flag still set after the forced teardown");

  // Fresh connect on the same camera object must perform a real handshake.
  NimBLEDevice::resetMock();
  NimBLEDevice::setMockPeer(&peer);
  peer.clearEvents();
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "fresh connect reaches active");
  check(peer.connected() && peer.tokenAccepted(), "fresh connect ran the real handshake");
  check(control.getConnectedTargetCount() == 1, "connected after false-connected guard");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// A transient link that misses a couple of attempts and then recovers must not
// leak clients and must eventually reach active under infinite reconnect.
bool scenarioTransientConnectRecovers() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  // Fail the first attempt, then recover. The interactive path retries at once
  // while the fail count is under two, so this stays snappy and still exercises
  // the client reclaim on the failed attempt.
  NimBLEDevice::setConnectFailCount(1);
  control.addActive(camera);
  control.connectAll(false);

  check(waitForState(Control::STATE_ACTIVE, 5000), "recovers to active after transient failure");
  check(control.getConnectedTargetCount() == 1, "connected after recovery");
  check(NimBLEDevice::liveClientCount() <= 1, "no client leak across transient failures");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// Client-pool exhaustion: with the pool capped and every connect failing, the
// repeated interactive retries must never leak beyond one live client and the
// control machine must not wedge. Recovery is possible once connects succeed.
bool scenarioClientPoolExhaustion() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  NimBLEDevice::setMaxClients(9);
  NimBLEDevice::setConnectShouldFail(true);

  control.addActive(camera);
  control.connectAll(false);
  // Interactive retries: failcount reaches the cap and lands in CONNECT_FAILED.
  check(waitForState(Control::STATE_CONNECT_FAILED, 5000), "failed connect ends in connect_failed");
  check(NimBLEDevice::liveClientCount() <= 1, "no client leak under pool pressure");
  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);

  // Recovery once the link is healthy again.
  NimBLEDevice::setConnectShouldFail(false);
  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "recovers to active after pool pressure");
  check(NimBLEDevice::liveClientCount() <= 1, "one live client after recovery");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// Two real Fujifilm Basic cameras share one Control session. The mock routes
// each connect by the camera's BLE address, so the assertions observe the real
// per-target handshake, shutter fan-out, one-link drop and targeted reconnect.
bool scenarioMultiConnectFujifilm() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera::Config firstConfig;
  firstConfig.name = "FUJIFILM X100VI A";
  firstConfig.address = NimBLEAddress(0x112233445501ULL, 0);
  firstConfig.token = {0x11, 0x22, 0x33, 0x44};
  FujifilmVirtualCamera::Config secondConfig;
  secondConfig.name = "FUJIFILM X100VI B";
  secondConfig.address = NimBLEAddress(0x112233445502ULL, 0);
  secondConfig.token = {0x55, 0x66, 0x77, 0x88};

  FujifilmVirtualCamera first(firstConfig);
  FujifilmVirtualCamera second(secondConfig);
  auto firstCamera = makeCamera(first);
  auto secondCamera = makeCamera(second);
  NimBLEDevice::setMockPeerForAddress(first.config().address, &first);
  NimBLEDevice::setMockPeerForAddress(second.config().address, &second);

  auto &power = Furble::Power::getInstance();
  control.addActive(firstCamera);
  control.addActive(secondCamera);
  check(control.getTargetCount() == 2, "two selected Fujifilm cameras become targets");

  control.connectAll(true);
  check(waitForState(Control::STATE_ACTIVE, 5000), "both Fujifilm cameras reach active");
  check(waitForConnectedCount(2, 1000), "both Fujifilm cameras remain connected");
  check(first.connected() && second.connected(), "both virtual peers see their own link");
  check(control.getDisconnectedName().empty(), "no camera is marked disconnected when whole");
  check(power.getCount(Furble::Power::LockType::NO_LIGHT_SLEEP) >= 1,
        "sleep lock held for the multi-connect session");

  first.clearEvents();
  second.clearEvents();
  control.sendCommand(Control::CMD_SHUTTER_PRESS);
  control.sendCommand(Control::CMD_SHUTTER_RELEASE);
  // Fujifilm sends a command and parameter write for each press/release. Four
  // writes on each peer proves the trigger reached both, not only the aggregate.
  check(waitForShutterWrites(first, 4, 2000), "first Fujifilm peer receives the shutter");
  check(waitForShutterWrites(second, 4, 2000), "second Fujifilm peer receives the shutter");

  // The last client is the second target because Control connects targets in
  // selection order. Drop only that link; the first camera must remain usable.
  NimBLEClient *droppedClient = NimBLEDevice::lastClient();
  check(droppedClient != nullptr, "the second target has a mock client");
  if (droppedClient != nullptr) {
    droppedClient->mockDropLink(0x08, /*fire_callback=*/true);
  }
  check(waitForConnectedCount(1, 3000), "one survivor stays connected after the drop");
  check(first.connected(), "the first Fujifilm peer survives the drop");
  check(!second.connected(), "only the second Fujifilm peer drops");
  check(control.getDisconnectedName() == second.config().name,
        "the dropped target is identified by name");

  first.clearEvents();
  second.clearEvents();
  control.sendCommand(Control::CMD_SHUTTER_PRESS);
  control.sendCommand(Control::CMD_SHUTTER_RELEASE);
  check(waitForShutterWrites(first, 4, 2000), "the live survivor still receives the shutter");
  check(shutterWriteCount(second) == 0, "the dropped peer receives no down-time shutter");

  check(waitForState(Control::STATE_ACTIVE, 5000), "only the dropped target reconnects");
  check(waitForConnectedCount(2, 1000), "the reconnect returns to two live targets");
  check(first.connected() && second.connected(), "both peers are live after reconnect");
  check(control.getDisconnectedName().empty(), "the per-device reconnect indicator clears");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

// The reconnect replay guard. A shutter issued while the target is not active
// must be dropped, never buffered on the control queue and flushed when the link
// returns. Bring a camera to active, force the link down and hold the reconnect
// open, fire a shutter while it is down, then let it reconnect and prove no
// buffered press replayed. Finally a shutter on the live link must still fire, so
// the drop guard did not wedge the normal path.
bool scenarioReconnectShutterDrop() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  control.addActive(camera);
  // Infinite reconnect so a mid-session drop re-enters the reconnect loop exactly
  // as it does on device.
  control.connectAll(true);
  check(waitForState(Control::STATE_ACTIVE, 5000), "reaches active within 5 s");
  check(control.getConnectedTargetCount() == 1, "one connected target");

  peer.clearEvents();
  check(shutterWriteCount(peer) == 0, "no shutter writes at baseline");

  // Make the reconnect's connect() block, the way a real reconnect blocks the
  // control task inside connectAll() for seconds. This is what lets a buffered
  // command survive on the control queue: while the control task is parked in
  // connect() it is not draining the queue, so anything enqueued during the
  // outage waits there and is flushed the instant the link is back. Without this
  // block a fast mock reconnect would drain and drop the command in a non-active
  // state, hiding the replay bug.
  NimBLEDevice::setConnectDelayMs(1200);

  // Silent supervision-timeout drop with the disconnect callback, so control
  // leaves active and starts reconnecting (and blocks in connect()).
  NimBLEClient *client = NimBLEDevice::lastClient();
  check(client != nullptr, "camera created a client");
  if (client != nullptr) {
    client->mockDropLink(0x08, /*fire_callback=*/true);
  }

  check(waitForNotState(Control::STATE_ACTIVE, 3000), "leaves active on the drop");
  // Let the control task reach and park inside the blocking connect().
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  check(!camera->isConnected(), "target link reads down while reconnecting");

  // Fire a shutter while the target is down and the control task is blocked in
  // connect(). It must be dropped, not parked on the control queue.
  control.sendCommand(Control::CMD_SHUTTER_PRESS);
  control.sendCommand(Control::CMD_SHUTTER_RELEASE);
  check(shutterWriteCount(peer) == 0, "shutter while down never reaches the peer");

  // Let the link recover. If the presses had been buffered on the control queue
  // they would flush here, the moment the state returns to active.
  check(waitForState(Control::STATE_ACTIVE, 5000), "reconnects to active");
  check(control.getConnectedTargetCount() == 1, "connected again after reconnect");

  // The crucial assertion: the press made while down must NOT replay now the link
  // is back. Poll a little to catch a late flush.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  check(shutterWriteCount(peer) == 0, "the down-time shutter never replays on reconnect");

  // A shutter on the recovered link must still fire, proving the drop guard did
  // not break the normal path.
  peer.clearEvents();
  control.sendCommand(Control::CMD_SHUTTER_PRESS);
  control.sendCommand(Control::CMD_SHUTTER_RELEASE);
  const uint32_t deadline = nowMs() + 2000;
  while (nowMs() < deadline && shutterWriteCount(peer) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check(shutterWriteCount(peer) >= 1, "a shutter on the live link still fires");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

const std::map<std::string, std::function<bool()>> &scenarios() {
  static const std::map<std::string, std::function<bool()>> table = {
      {"fresh-connect",                    scenarioFreshConnect                },
      {"dead-camera-disconnect-no-freeze", scenarioDeadCameraDisconnectNoFreeze},
      {"connect-after-dead-disconnect",    scenarioConnectAfterDeadDisconnect  },
      {"stale-session-reconnect",          scenarioStaleSessionReconnect       },
      {"false-connected-guard",            scenarioFalseConnectedGuard         },
      {"transient-connect-recovers",       scenarioTransientConnectRecovers    },
      {"client-pool-exhaustion",           scenarioClientPoolExhaustion        },
      {"multi-connect-fujifilm",           scenarioMultiConnectFujifilm        },
      {"reconnect-shutter-drop",           scenarioReconnectShutterDrop        },
  };
  return table;
}

int runOne(const std::string &name) {
  auto it = scenarios().find(name);
  if (it == scenarios().end()) {
    std::cerr << "unknown scenario: " << name << '\n';
    return 2;
  }
  std::cout << "control-e2e scenario: " << name << '\n';
  startControlTask();
  g_Failures = 0;
  const bool ok = it->second();
  resetControl();
  if (!ok || g_Failures != 0) {
    std::cout << "control-e2e " << name << ": FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-e2e " << name << ": PASS\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc >= 2) {
    return runOne(argv[1]);
  }

  // No argument: run every scenario in one process.
  startControlTask();
  int rc = 0;
  for (const auto &entry : scenarios()) {
    std::cout << "control-e2e scenario: " << entry.first << '\n';
    g_Failures = 0;
    const bool ok = entry.second();
    resetControl();
    if (!ok || g_Failures != 0) {
      std::cout << "control-e2e " << entry.first << ": FAIL (" << g_Failures << " checks)\n";
      rc = 1;
    } else {
      std::cout << "control-e2e " << entry.first << ": PASS\n";
    }
  }
  return rc;
}
