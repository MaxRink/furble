// Seeded fuzzer over the real Furble::Control BLE connect/disconnect/reconnect
// lifecycle.
//
// This compiles the production src/FurbleControl.cpp and the real lib/furble
// Camera and runs them on real FreeRTOS-shim threads against the shared
// MockNimBLE peer and the Fujifilm virtual camera, exactly like the control
// end-to-end harness. Where that harness runs a fixed set of hand-written
// scenarios, this driver builds RANDOMISED sequences of lifecycle operations
// with injected BLE faults from a SEEDED PRNG, so the same seed always replays
// the same sequence. After every operation it asserts a battery of invariants:
//
//   - no crash / no ASan-or-UBSan error (the target links with the sanitizers,
//     which is how the reclaim use-after-free class is caught);
//   - state and target-count stay consistent (never ACTIVE with no connected
//     client, never a held sleep lock once idle);
//   - the NimBLE client pool never leaks (one client per connected camera by
//     design; growth across reconnect cycles is the leak);
//   - nothing wedges (every operation completes inside a generous time bound, so
//     the ~30 s interactive-disconnect freeze class shows up as a real overrun);
//   - a connect that reports ACTIVE actually ran the handshake (peer.connected()
//     and peer.tokenAccepted()), and its shutter actually reaches the peer.
//
// Each operation is a self-contained template that starts and ends at
// STATE_IDLE and reuses one persistent Camera, so a client leak or a stuck-state
// bug accumulates across the sequence the way it would on a device carrying one
// camera on its saved list. The faults injected are the same classes hunted by
// hand this session: connect-fail-N, client-pool exhaustion, stale-session
// reconnect, withheld ATT write, mid-handshake link drop, silent power-off drop
// with a delayed supervision timeout, and the deferred client-delete model that
// mirrors how real NimBLE hides the reclaim use-after-free.
//
// The default fault set is the "safe" set: every fault here is one the fixed
// production code is expected to survive, so the fuzz target stays green and
// guards against regressions and new bugs. The two FujifilmBasic missing-shutter
// findings (a missing shutter service once crashed the connect, a missing
// shutter characteristic once silently disabled the shutter) are now fixed:
// FujifilmBasic::_connect returns false in both cases. They are kept as
// seed-pinned regression guards driven through the --repro modes below and run
// as normal passing tests.

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

const char *LOG_TAG = "furble-control-fuzz";

namespace {

using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;

// Generous per-operation bounds. Real operations complete in tens of
// milliseconds under the mock; an overrun means a genuine wedge.
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t DISCONNECT_BOUND_MS = 8000;
constexpr uint32_t SHUTTER_TIMEOUT_MS = 2000;

int g_Failures = 0;
std::string g_Context;    // current seed + operation, printed on a failure
std::string g_FirstFail;  // context of the first finding, for the summary line

void fail(const std::string &message) {
  std::cerr << "  FINDING [" << g_Context << "]: " << message << '\n';
  if (g_Failures == 0) {
    g_FirstFail = g_Context;
  }
  g_Failures++;
}

bool check(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return control.getState() == want;
}

bool waitUntil(const std::function<bool()> &predicate, uint32_t timeout_ms) {
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

std::shared_ptr<Furble::FujifilmBasic> makeCamera(FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

void startControlTask() {
  static std::atomic<bool> started {false};
  bool expected = false;
  if (started.compare_exchange_strong(expected, true)) {
    auto &control = Control::getInstance();
    xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);
  }
}

// One shared camera and peer per iteration, plus the seeded PRNG.
struct FuzzContext {
  std::mt19937 &rng;
  FujifilmVirtualCamera &peer;
  std::shared_ptr<Furble::FujifilmBasic> camera;
  uint32_t seed = 0;
};

uint32_t sleepLock() {
  return Furble::Power::getInstance().getCount(Furble::Power::LockType::NO_LIGHT_SLEEP);
}

// Leak probe. Reap the clients a link-loss drop queued for asynchronous free
// (their supervision-timeout free on hardware), then assert the live pool never
// exceeds one client for the single fuzzed camera. Growth here is the pool leak.
void checkNoLeak(const char *where) {
  NimBLEDevice::reapDeferredClients();
  const size_t live = NimBLEDevice::liveClientCount();
  check(live <= 1, std::string("client pool leak (") + std::to_string(live) + " live) at " + where);
}

// Invariants that must hold whenever the machine has settled at STATE_ACTIVE.
void checkActiveInvariants(FuzzContext &ctx, const char *where) {
  auto &control = Control::getInstance();
  check(control.getState() == Control::STATE_ACTIVE, std::string("expected active at ") + where);
  check(control.getTargetCount() >= 1, std::string("active with no target at ") + where);
  check(control.getConnectedTargetCount() == control.getTargetCount(),
        std::string("active but not all targets connected at ") + where);
  check(control.allConnected(), std::string("active but allConnected() false at ") + where);
  check(ctx.camera->isConnected(), std::string("active but camera not connected at ") + where);
  // A connect that reports ACTIVE must actually have run the handshake against a
  // live peer link, not short-circuited on a stale flag.
  check(ctx.peer.connected(), std::string("active but peer link is down at ") + where);
  check(ctx.peer.tokenAccepted(), std::string("active but handshake never ran at ") + where);
  check(sleepLock() >= 1, std::string("active but sleep lock not held at ") + where);
  checkNoLeak(where);
}

// Invariants that must hold whenever the machine has settled at STATE_IDLE.
void checkIdleInvariants(const char *where) {
  auto &control = Control::getInstance();
  check(control.getState() == Control::STATE_IDLE, std::string("expected idle at ") + where);
  check(control.getTargetCount() == 0, std::string("idle with targets at ") + where);
  check(sleepLock() == 0, std::string("sleep lock still held once idle at ") + where);
  checkNoLeak(where);
}

// Press the shutter through the real control command path and confirm the write
// reaches the peer. A connected camera whose shutter is silently dead fails here.
bool shutterReachesPeer(FuzzContext &ctx) {
  ctx.peer.clearEvents();
  Control::getInstance().sendCommand(Control::CMD_SHUTTER_PRESS);
  const std::string shutterChar = FujifilmVirtualCamera::shutterCharacteristicUUID().toString();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), SHUTTER_TIMEOUT_MS)) {
    for (const auto &write : ctx.peer.writes()) {
      if (write.characteristic == shutterChar) {
        Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
  return false;
}

// Bring the machine to idle. Used to normalise between operations and to unwind
// a template that could not reach its target state.
//
// When deadLink is set the camera link was silently severed (powered off) and
// the interactive teardown would otherwise wait for a supervision event that the
// mock does not model, so a background helper stands in for the link supervision
// timeout and delivers the delayed onDisconnect, exactly as the m_IdleTimeout cap
// bounds it on hardware. The helper is used only for the dead-link case: on a
// live link the target task's clean teardown frees the client inline, and firing
// mockDropLink on it concurrently would be a harness self-race, not a product
// bug.
bool disconnectToIdle(FuzzContext &ctx, bool restart, bool deadLink = false) {
  auto &control = Control::getInstance();
  NimBLEClient *client = NimBLEDevice::lastClient();

  std::thread supervision;
  if (deadLink && !restart && (client != nullptr)) {
    supervision = std::thread([client]() {
      // Stand in for the bounded link supervision timeout: after a short delay
      // deliver the onDisconnect the powered-off camera never sent, so the
      // interactive teardown unblocks.
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      client->mockDropLink(0x08, /*fire_callback=*/true);
    });
  }

  const uint32_t start = nowMs();
  const bool completed =
      restart ? control.disconnect(300, /*forRestart=*/true) : control.disconnect();
  const uint32_t elapsed = nowMs() - start;
  if (supervision.joinable()) {
    supervision.join();
  }

  check(completed || restart, "interactive disconnect reported incomplete");
  check(elapsed < DISCONNECT_BOUND_MS,
        std::string("disconnect wedged for ") + std::to_string(elapsed) + " ms");
  const bool idle = waitForState(Control::STATE_IDLE, 3000);
  check(idle, "disconnect did not return to idle");
  return idle;
}

// A fresh connect to a healthy camera must reach ACTIVE and drive the shutter.
void opConnectClean(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  const bool infinite = (ctx.rng() & 1) != 0;
  control.addActive(ctx.camera);
  control.connectAll(infinite);
  if (!check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
             "clean connect never active")) {
    disconnectToIdle(ctx, false);
    return;
  }
  checkActiveInvariants(ctx, "connect-clean");
  check(shutterReachesPeer(ctx), "shutter did not reach peer on a healthy connect");
  disconnectToIdle(ctx, (ctx.rng() & 1) != 0);
  checkIdleInvariants("connect-clean/idle");
}

// A transient run of failed connects must reclaim each client, settle in a
// terminal state without wedging, and recover on a user retry once the link
// heals. The interactive connect deliberately gives up after two failed attempts
// (STATE_CONNECT_FAILED), so more than one transient failure legitimately needs a
// user retry rather than auto-recovering; the invariants are no wedge, no leak,
// and recovery is possible.
void opTransientConnectFail(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  const size_t fails = 1 + (ctx.rng() % 3);  // 1..3 transient connect failures
  NimBLEDevice::setConnectFailCount(fails);
  control.addActive(ctx.camera);
  control.connectAll(false);

  // The machine must reach a terminal state (active, or gave-up) without wedging.
  const bool terminal = waitUntil(
      []() {
        const auto state = Control::getInstance().getState();
        return (state == Control::STATE_ACTIVE) || (state == Control::STATE_CONNECT_FAILED);
      },
      CONNECT_TIMEOUT_MS);
  check(terminal, "transient connect failures wedged before a terminal state");
  checkNoLeak("transient-connect-fail/attempts");
  NimBLEDevice::setConnectFailCount(0);

  // If it gave up, a user retry with the link now healthy must recover.
  if (control.getState() != Control::STATE_ACTIVE) {
    disconnectToIdle(ctx, false);
    control.addActive(ctx.camera);
    control.connectAll(false);
    if (!check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
               "did not recover on a user retry after transient failures")) {
      disconnectToIdle(ctx, false);
      return;
    }
  }
  checkActiveInvariants(ctx, "transient-connect-fail");
  disconnectToIdle(ctx, false);
  checkIdleInvariants("transient-connect-fail/idle");
}

// The pool caps at nine clients and every connect fails, so the interactive
// retries must never leak past one live client and must land in CONNECT_FAILED.
// Recovery once the link heals must reach ACTIVE.
void opPoolExhaustion(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  NimBLEDevice::setMaxClients(9);
  NimBLEDevice::setConnectShouldFail(true);
  control.addActive(ctx.camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS),
        "pool exhaustion did not settle in connect_failed");
  checkNoLeak("pool-exhaustion/failed");
  disconnectToIdle(ctx, false);

  NimBLEDevice::setConnectShouldFail(false);
  NimBLEDevice::setMaxClients(0);
  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            "did not recover to active after pool exhaustion")) {
    checkActiveInvariants(ctx, "pool-exhaustion/recovered");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("pool-exhaustion/idle");
}

// A stale-session reconnect (the camera still holds the prior CCCD session) must
// still complete and reach a connected active state, bounded.
void opStaleSessionReconnect(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  control.addActive(ctx.camera);
  control.connectAll(false);
  if (!check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS), "stale: first connect")) {
    disconnectToIdle(ctx, false);
    return;
  }
  disconnectToIdle(ctx, false);

  ctx.peer.setStaleSubscribeSession(true);
  ctx.peer.clearEvents();
  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            "stale-session reconnect never active")) {
    checkActiveInvariants(ctx, "stale-session-reconnect");
  }
  ctx.peer.setStaleSubscribeSession(false);
  disconnectToIdle(ctx, false);
  checkIdleInvariants("stale-session-reconnect/idle");
}

// A rejected handshake write must abort the connect, leave the camera
// disconnected, and reclaim the client, then a healthy connect must still work.
void opWriteFailAbort(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  ctx.peer.failWrite(FujifilmVirtualCamera::pairServiceUUID(),
                     FujifilmVirtualCamera::pairCharacteristicUUID());
  control.addActive(ctx.camera);
  control.connectAll(false);
  // Every attempt aborts at the pair write, so the interactive path must give up
  // in a terminal state without wedging and without ever reporting connected.
  check(waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS),
        "rejected handshake write did not settle in connect_failed");
  check(control.getState() != Control::STATE_ACTIVE,
        "camera reached active despite a rejected handshake write");
  ctx.peer.clearFaults();
  disconnectToIdle(ctx, false);
  checkNoLeak("write-fail-abort/idle");

  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            "healthy connect failed after a write-fail abort")) {
    checkActiveInvariants(ctx, "write-fail-abort/recovered");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("write-fail-abort/idle2");
}

// A supervision-timeout link loss mid-handshake must unwind cleanly and reclaim
// the client, then a healthy connect must recover.
void opMidHandshakeDrop(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  // Drop the link on the identify write, part way through the handshake.
  ctx.peer.dropLinkOnWrite(FujifilmVirtualCamera::pairServiceUUID(),
                           FujifilmVirtualCamera::identifierCharacteristicUUID());
  control.addActive(ctx.camera);
  control.connectAll(false);
  // Every attempt drops mid-handshake, so the interactive path must give up in a
  // terminal state without wedging and without ever reporting connected.
  check(waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS),
        "mid-handshake drop did not settle in connect_failed");
  check(control.getState() != Control::STATE_ACTIVE,
        "camera reached active despite a mid-handshake drop");
  ctx.peer.clearFaults();
  disconnectToIdle(ctx, false);
  checkNoLeak("mid-handshake-drop/idle");

  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            "healthy connect failed after a mid-handshake drop")) {
    checkActiveInvariants(ctx, "mid-handshake-drop/recovered");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("mid-handshake-drop/idle2");
}

// A live camera drops (silent power-off) and the supervision timeout then fires:
// the control machine must auto-reconnect back to ACTIVE without leaking the old
// client. Models the reconnect-after-drop path with the deferred-delete model.
void opDropAutoReconnect(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  control.addActive(ctx.camera);
  control.connectAll(false);
  if (!check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
             "drop-recon: first connect")) {
    disconnectToIdle(ctx, false);
    return;
  }

  NimBLEClient *client = NimBLEDevice::lastClient();
  if (client != nullptr) {
    client->mockDropLink(0x08, /*fire_callback=*/false);  // silent power-off
    client->mockDropLink(0x08, /*fire_callback=*/true);   // supervision timeout
  }

  // The control task must notice the link fell and reconnect. Wait for the full
  // reconverge (active AND all targets connected AND the peer link back up), not
  // just the stale pre-drop ACTIVE that has not yet transitioned.
  auto &c = control;
  const bool reconverged = waitUntil(
      [&c, &ctx]() {
        return (c.getState() == Control::STATE_ACTIVE)
               && (c.getConnectedTargetCount() == c.getTargetCount()) && ctx.peer.connected();
      },
      CONNECT_TIMEOUT_MS);
  if (check(reconverged, "did not auto-reconnect after a link drop")) {
    checkActiveInvariants(ctx, "drop-auto-reconnect");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("drop-auto-reconnect/idle");
}

// A dead-camera interactive disconnect must return promptly (no ~30 s freeze).
void opDeadCameraDisconnect(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  control.addActive(ctx.camera);
  control.connectAll(false);
  if (!check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS), "dead-cam: first connect")) {
    disconnectToIdle(ctx, false);
    return;
  }
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (client != nullptr) {
    client->mockDropLink(0x08, /*fire_callback=*/false);  // powered off, stale flag stays
  }
  check(ctx.camera->isConnected(), "dead-camera stale connected flag did not persist");
  // disconnectToIdle runs the bounded supervision helper and asserts the bound.
  disconnectToIdle(ctx, false, /*deadLink=*/true);
  checkIdleInvariants("dead-camera-disconnect/idle");
}

// A peer that resets (a power-cycle) at a RANDOMISED point across the connect
// handshake, not the single fixed identify-write point opMidHandshakeDrop uses.
// The bench crash was a camera power-cycled mid-connect, and where in the
// handshake the reset lands decides which half-built client state the connect
// path is holding when the link falls: the seeded PRNG picks the write phase
// (the pairing token exchange, then the identifier write) and the fault flavour
// (a supervision-timeout link loss, or an ATT write the peer rejects), so across
// a fuzz run a fault lands at more than one point in the connect sequence. Each
// must abort into a terminal non-active state without wedging, crashing or
// leaking, and a healthy connect after the fault clears must still recover.
//
// This uses only the peer fault levers already on master (dropLinkOnWrite,
// failWrite), so it stays additive and does not touch the shared MockNimBLE or
// FujifilmVirtualCamera symbols. The deeper mid-connect point, where the reset
// self-frees the client inline while _connect() is still running (the exact
// use-after-free crash class), needs the dropLinkDuringConnect /
// mockDropLinkSelfDelete hook that the fix/connect-crash-mid-drop branch adds to
// those shared files. When that branch lands, the opMidConnectSelfDeleteDrop
// stub below (guarded by FURBLE_FUZZ_HAS_DROP_DURING_CONNECT) should be enabled
// and added to the table; see the note there for the merge reconciliation.
void opHandshakePhaseDrop(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  struct Phase {
    const char *name;
    NimBLEUUID service;
    NimBLEUUID characteristic;
  };
  const std::array<Phase, 2> phases = {
      {{"token", FujifilmVirtualCamera::pairServiceUUID(),
        FujifilmVirtualCamera::pairCharacteristicUUID()},
       {"identifier", FujifilmVirtualCamera::pairServiceUUID(),
        FujifilmVirtualCamera::identifierCharacteristicUUID()}}
  };
  const Phase &phase = phases[ctx.rng() % phases.size()];
  const bool dropFlavour = (ctx.rng() & 1) != 0;
  if (dropFlavour) {
    ctx.peer.dropLinkOnWrite(phase.service, phase.characteristic);
  } else {
    ctx.peer.failWrite(phase.service, phase.characteristic);
  }
  control.addActive(ctx.camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS),
        std::string("handshake fault at ") + phase.name + " did not settle in connect_failed");
  check(control.getState() != Control::STATE_ACTIVE,
        std::string("reached active despite a handshake fault at ") + phase.name);
  ctx.peer.clearFaults();
  disconnectToIdle(ctx, false);
  checkNoLeak("handshake-phase-drop/idle");

  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            std::string("healthy connect failed after a handshake fault at ") + phase.name)) {
    checkActiveInvariants(ctx, "handshake-phase-drop/recovered");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("handshake-phase-drop/idle2");
}

#if defined(FURBLE_FUZZ_HAS_DROP_DURING_CONNECT)
// Merge-reconciliation stub. Enabled once fix/connect-crash-mid-drop lands the
// dropLinkDuringConnect / mockDropLinkSelfDelete hook on the shared peer and mock
// (define FURBLE_FUZZ_HAS_DROP_DURING_CONNECT then add this to the ops table).
// Unlike opHandshakePhaseDrop, the write completes and the peer then severs the
// link with an inline self-deleting client free while _connect() keeps going, so
// its next m_Client dereference lands on the freed client: the mid-connect
// use-after-free. Under ASan/UBSan a regression there is a hard finding, and on
// the fixed code the connect aborts cleanly and recovers.
void opMidConnectSelfDeleteDrop(FuzzContext &ctx) {
  auto &control = Control::getInstance();
  ctx.peer.dropLinkDuringConnect(FujifilmVirtualCamera::pairServiceUUID(),
                                 FujifilmVirtualCamera::identifierCharacteristicUUID());
  control.addActive(ctx.camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS),
        "mid-connect self-delete drop did not settle in connect_failed");
  check(control.getState() != Control::STATE_ACTIVE,
        "reached active despite a mid-connect self-delete drop");
  ctx.peer.clearFaults();
  disconnectToIdle(ctx, false);
  checkNoLeak("mid-connect-selfdelete/idle");

  control.addActive(ctx.camera);
  control.connectAll(false);
  if (check(waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS),
            "healthy connect failed after a mid-connect self-delete drop")) {
    checkActiveInvariants(ctx, "mid-connect-selfdelete/recovered");
  }
  disconnectToIdle(ctx, false);
  checkIdleInvariants("mid-connect-selfdelete/idle2");
}
#endif

using Operation = std::function<void(FuzzContext &)>;

const std::vector<std::pair<const char *, Operation>> &operations() {
  static const std::vector<std::pair<const char *, Operation>> table = {
      {"connect-clean",           opConnectClean            },
      {"transient-connect-fail",  opTransientConnectFail    },
      {"pool-exhaustion",         opPoolExhaustion          },
      {"stale-session-reconnect", opStaleSessionReconnect   },
      {"write-fail-abort",        opWriteFailAbort          },
      {"mid-handshake-drop",      opMidHandshakeDrop        },
      {"handshake-phase-drop",    opHandshakePhaseDrop      },
      {"drop-auto-reconnect",     opDropAutoReconnect       },
      {"dead-camera-disconnect",  opDeadCameraDisconnect    },
#if defined(FURBLE_FUZZ_HAS_DROP_DURING_CONNECT)
      {"mid-connect-selfdelete",  opMidConnectSelfDeleteDrop},
#endif
  };
  return table;
}

void freshEnvironment() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);
  // Faithfully honour setSelfDelete and defer live deleteClient, so the live
  // client count is a sound leak probe and a post-free dereference is caught.
  NimBLEDevice::setDeferredClientDelete(true);
}

// Run one seeded iteration: a random-length random sequence of operations over a
// single persistent camera, asserting invariants throughout.
int runIteration(uint32_t seed) {
  std::mt19937 rng(seed);
  freshEnvironment();

  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  FuzzContext ctx {rng, peer, camera, seed};

  const auto &ops = operations();
  const int steps = 4 + static_cast<int>(rng() % 6);  // 4..9 operations
  for (int i = 0; i < steps; i++) {
    const size_t pick = rng() % ops.size();
    g_Context =
        "seed=" + std::to_string(seed) + " step=" + std::to_string(i) + " op=" + ops[pick].first;
    ops[pick].second(ctx);
    // Normalise: every operation is designed to end at idle. If one left the
    // machine elsewhere, unwind it so the next operation starts clean.
    if (Control::getInstance().getState() != Control::STATE_IDLE) {
      disconnectToIdle(ctx, true);
    }
  }

  // Return to idle and drop the camera before the peer goes out of scope.
  disconnectToIdle(ctx, true);
  return g_Failures;
}

// --- Regression guards for the two FujifilmBasic missing-shutter findings -----
//
// These run the real control lifecycle with a fault that once tripped a
// FujifilmBasic bug. Both bugs are now fixed (FujifilmBasic::_connect returns
// false when the shutter service or characteristic is missing), so each guard
// runs clean as a normal passing test. A crash or a violated invariant here
// means the fix regressed.

// Missing shutter service: FujifilmBasic::_connect once dereferenced a null
// shutter service pointer part way through the handshake, crashing the control
// task (UBSan traps the null member call) and aborting the whole process. It now
// returns false instead. This still runs inside a forked child (see main) so a
// regression that reintroduces the crash is caught as the child's signal death
// rather than taking down the test binary. On the fixed code the connect fails
// cleanly (no crash), the machine lands in a terminal non-active state, and this
// returns 0. The contract is simply: driving the connect must not crash.
int reproMissingShutterService(uint32_t seed) {
  freshEnvironment();
  FujifilmVirtualCamera peer;
  peer.suppressService(FujifilmVirtualCamera::shutterServiceUUID());
  auto camera = makeCamera(peer);
  std::mt19937 rng(seed);
  FuzzContext ctx {rng, peer, camera, seed};

  auto &control = Control::getInstance();
  control.addActive(camera);
  control.connectAll(false);  // control task drives Camera::connect -> crash on buggy code
  // If the code is fixed the connect fails cleanly and the machine gives up; reach
  // here without crashing means the finding is fixed.
  waitForState(Control::STATE_CONNECT_FAILED, CONNECT_TIMEOUT_MS);
  disconnectToIdle(ctx, true);
  return g_Failures;  // stays 0: no crash observed means the bug is fixed
}

// Missing shutter characteristic: the connect once reported ACTIVE while the
// shutter was silently dead. FujifilmBasic::_connect now returns false instead,
// so the machine never reaches ACTIVE. No crash; the guard passes.
int reproMissingShutterChar(uint32_t seed) {
  freshEnvironment();
  FujifilmVirtualCamera peer;
  peer.suppressCharacteristic(FujifilmVirtualCamera::shutterServiceUUID(),
                              FujifilmVirtualCamera::shutterCharacteristicUUID());
  auto camera = makeCamera(peer);
  std::mt19937 rng(seed);
  FuzzContext ctx {rng, peer, camera, seed};

  auto &control = Control::getInstance();
  control.addActive(camera);
  control.connectAll(false);
  // Fixed contract: the connect must never report a connected camera whose
  // shutter is silently dead. FujifilmBasic::_connect now returns false when the
  // shutter characteristic is missing, so the machine settles in a terminal
  // non-active state. If it ever does reach ACTIVE the shutter must reach the
  // peer. Either way g_Failures stays 0 and the guard passes.
  if (waitForState(Control::STATE_ACTIVE, CONNECT_TIMEOUT_MS)) {
    check(shutterReachesPeer(ctx),
          "connect reported active but the shutter is silently dead (missing shutter char)");
  }
  disconnectToIdle(ctx, true);
  return g_Failures;
}

// Deterministic guard for the handshake-phase fault sweep. Runs the operation
// across a fixed seed span so both handshake write points (token, identifier)
// and both fault flavours (drop, rejected write) are exercised every run, rather
// than relying on a random table pick landing on them. On the fixed code every
// combination aborts cleanly and recovers, so this returns 0; a crash, wedge,
// leak or a connect that wrongly reports active is a finding.
int reproHandshakePhaseDrop(uint32_t seed) {
  freshEnvironment();
  FujifilmVirtualCamera peer;
  auto camera = makeCamera(peer);
  for (uint32_t i = 0; i < 8; i++) {
    std::mt19937 rng(seed + i);
    FuzzContext ctx {rng, peer, camera, seed + i};
    g_Context = "repro=handshake-phase-drop sub=" + std::to_string(i);
    opHandshakePhaseDrop(ctx);
    if (Control::getInstance().getState() != Control::STATE_IDLE) {
      disconnectToIdle(ctx, true);
    }
  }
  return g_Failures;
}

int usage(const char *argv0) {
  std::cerr << "usage: " << argv0 << " <seed> [iterations]\n"
            << "       " << argv0
            << " --repro missing-shutter-service|missing-shutter-char|handshake-phase-drop "
               "[seed]\n";
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  FurbleHostTaskScope taskScope;
  if ((argc >= 2) && (std::strcmp(argv[1], "--repro") == 0)) {
    if (argc < 3) {
      return usage(argv[0]);
    }
    const uint32_t seed =
        (argc >= 4) ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 1;

    if (std::strcmp(argv[2], "missing-shutter-service") == 0) {
      // A regression that reintroduces the crash would abort the process, so run
      // the connect in a forked child. The child starts its own control task; a
      // signal death is reported to CTest as a non-zero exit (the guard fails).
      // The fixed connect exits the child cleanly (0) and the guard passes.
      std::cout << "control-fuzz repro missing-shutter-service seed=" << seed << '\n';
      const pid_t pid = fork();
      if (pid == 0) {
        startControlTask();
        _exit(reproMissingShutterService(seed) == 0 ? 0 : 1);
      }
      int status = 0;
      waitpid(pid, &status, 0);
      if (WIFSIGNALED(status)) {
        std::cout << "repro: reproduced (connect crashed, signal " << WTERMSIG(status) << ")\n";
        return 1;
      }
      const bool clean = WIFEXITED(status) && (WEXITSTATUS(status) == 0);
      std::cout << (clean ? "repro: clean (finding is fixed)\n" : "repro: reproduced\n");
      return clean ? 0 : 1;
    }

    if (std::strcmp(argv[2], "missing-shutter-char") == 0) {
      startControlTask();
      std::cout << "control-fuzz repro missing-shutter-char seed=" << seed << '\n';
      reproMissingShutterChar(seed);
      std::cout << (g_Failures == 0 ? "repro: clean (finding is fixed)\n" : "repro: reproduced\n");
      return g_Failures == 0 ? 0 : 1;
    }

    if (std::strcmp(argv[2], "handshake-phase-drop") == 0) {
      startControlTask();
      std::cout << "control-fuzz repro handshake-phase-drop seed=" << seed << '\n';
      reproHandshakePhaseDrop(seed);
      std::cout << (g_Failures == 0 ? "repro: clean (survives handshake-phase faults)\n"
                                    : "repro: reproduced\n");
      return g_Failures == 0 ? 0 : 1;
    }

    return usage(argv[0]);
  }

  startControlTask();

  // Seed comes from argv or the FUZZ_SEED env var, never from an unseeded rand.
  uint32_t seed = 0;
  if (argc >= 2) {
    seed = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  } else if (const char *env = std::getenv("FUZZ_SEED")) {
    seed = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
  } else {
    return usage(argv[0]);
  }

  int iterations = 12;
  if (argc >= 3) {
    iterations = std::atoi(argv[2]);
  } else if (const char *env = std::getenv("FUZZ_ITERS")) {
    iterations = std::atoi(env);
  }

  std::cout << "control-fuzz seed=" << seed << " iterations=" << iterations << '\n';
  for (int i = 0; i < iterations; i++) {
    // Derive a distinct, deterministic sub-seed per iteration from the base seed.
    const uint32_t subSeed = seed * 2654435761u + static_cast<uint32_t>(i);
    runIteration(subSeed);
    if (g_Failures != 0) {
      std::cout << "control-fuzz FAIL: " << g_Failures << " finding(s); first at " << g_FirstFail
                << " (replay with seed " << seed << ")\n";
      return 1;
    }
  }
  std::cout << "control-fuzz PASS: " << iterations << " iterations clean for seed " << seed << '\n';
  return 0;
}
