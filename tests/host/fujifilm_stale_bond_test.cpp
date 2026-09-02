// Host regression tests for the Fujifilm Secure stale-bond recovery.
//
// Evidence base: the 2026-09-02 X100VI bench run
// (bench-logs/stale-bond-245-run2). furble's pairing was deleted on the camera
// only, so furble kept both its saved entry and its local bond. Every saved
// reconnect then came up at the link level and failed the encryption handshake
// with "rc=13 Operation timed out" after ~30 s or "rc=520 Connection Timeout"
// after ~5 s, then disconnected and retried, forever. No definitive refusal
// ever arrived, and rc=520 wakes the connect task with the disconnect event
// still queued, so link state cannot separate the two shapes either.
//
// The recovery therefore triggers on a run of consecutive security failures on
// a camera that was bonded when the attempt started: delete the stale local
// bond once, try one fresh in-link pairing (which works if the camera is
// already in pairing mode), and otherwise flag the camera as needing a re-pair
// so Control can stop the cycle instead of looping. The #232/#239 registration
// gate still decides acceptance after the link is secured.

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "Device.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "SecureTimeoutPeer.h"
#include "advertisement_scan_stub.h"

const char *LOG_TAG = "furble-stale-bond-test";

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

void init() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
}

// Wait until the connect thread has the link up, which is the moment before it
// enters the blocking security wait. A fixed sleep would race a loaded runner:
// cancel too early and the wait has not been entered, so the test then sits out
// the whole block and fails for the wrong reason.
bool waitForLink(const Furble::Host::FujifilmVirtualCamera &peer, uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (peer.connected()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return peer.connected();
}

Furble::Host::FujifilmVirtualCamera::Config secureConfig() {
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  return config;
}

// The bench signature. The camera deleted its pairing and is not in pairing
// mode, so every handshake times out and takes the link with it. One timeout
// must change nothing; the second is the verdict.
void testTimeoutRunDeletesBondAndFlagsRepair() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(Furble::Host::FujifilmVirtualCamera::kSecureTimeoutAlways);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "first attempt fails on the security timeout");
  check(NimBLEDevice::deleteBondCount() == 0, "one timeout is not evidence, the bond survives");
  check(!camera.needsRepair(), "one timeout does not ask the user to re-pair");

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "second attempt fails on the security timeout");
  check(NimBLEDevice::deleteBondCount() == 1, "the second consecutive timeout deletes the bond");
  check(!NimBLEDevice::isBonded(camera.getAddress()), "the dead keys are gone");
  check(camera.needsRepair(), "the camera is flagged as needing a re-pair");

  // Further attempts start unbonded, so there is nothing left to delete and the
  // recovery must not fire again.
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "a third attempt still fails");
  check(NimBLEDevice::deleteBondCount() == 1, "the recovery deletes the bond exactly once");
}

// The rc=520 shape: the handshake failure reaches the connect task before the
// disconnect event is delivered, so the client still reports connected. Same
// verdict, reached through the shared SecureTimeoutPeer decorator.
void testEventPendingTimeoutRunReachesTheSameVerdict() {
  init();
  Furble::Host::FujifilmVirtualCamera inner(secureConfig());
  Furble::Host::SecureTimeoutPeer peer(inner, 520);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = inner.advertisement();

  NimBLEDevice::setBonded(true);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "first rc=520 attempt fails");
  check(NimBLEDevice::deleteBondCount() == 0, "a single rc=520 keeps the bond");
  check(!camera.needsRepair(), "a single rc=520 does not ask the user to re-pair");

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "second rc=520 attempt fails");
  check(NimBLEDevice::deleteBondCount() == 1, "the second consecutive rc=520 deletes the bond");
  check(camera.needsRepair(), "the camera is flagged as needing a re-pair");
}

// A lone timeout is radio noise. The next attempt succeeds, so the bond must
// still be there and the run must have been forgotten.
void testSingleTimeoutThenSuccessKeepsBond() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(1);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the transient timeout fails the attempt");
  check(NimBLEDevice::deleteBondCount() == 0, "a transient security timeout keeps the bond");

  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the next attempt connects normally");
  check(NimBLEDevice::deleteBondCount() == 0, "a recovered camera never loses its bond");
  check(NimBLEDevice::isBonded(camera.getAddress()), "the saved bond survives");
  check(!camera.needsRepair(), "no re-pair prompt for a camera that came back");
  camera.disconnect();
}

// The camera deleted its pairing but is sitting in pairing mode: it refuses the
// dead keys while staying on the link. Once the stale bond is deleted the fresh
// pairing goes through on the same link and the connect proceeds to
// registration, so the user never sees a prompt.
void testInLinkFreshPairProceedsToRegistration() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setRefuseWhileBonded(true);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first refusal is not yet evidence");
  check(NimBLEDevice::deleteBondCount() == 0, "one refusal keeps the bond");

  check(camera.connect(ESP_PWR_LVL_P3, 1000),
        "the second refusal deletes the bond and the in-link fresh pair connects");
  check(NimBLEDevice::deleteBondCount() == 1, "the recovery deletes the bond exactly once");
  check(!camera.needsRepair(), "an in-link fresh pair needs no user re-pair");
  check(peer.configured(), "the fresh pairing proceeded through registration");
  camera.disconnect();
}

// No prior bond means there is nothing stale to delete: a refused first
// pairing must not touch the bond store however often it is retried.
void testUnbondedRefusalDeletesNothing() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(false);
  peer.setSecureConnectionResult(false);

  Furble::FujifilmSecure camera(&advertisement);
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "connect fails when a first pairing is refused");
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "a retried first pairing fails too");
  check(NimBLEDevice::deleteBondCount() == 0, "no bond delete without a prior bond");
  check(!camera.needsRepair(), "an unpaired camera is not a stale bond");
}

// The 2026-09-02 follow-up bench run found the other half of the problem: with
// the camera-side pairing gone, a UI-driven connect locked the device. NimBLE
// holds the connect task inside secureConnection() for the whole pairing
// timeout, up to 30 s for rc=13, and the plan-148 cancel token is only polled
// around that call, so a user cancel did nothing and the screen appeared
// frozen. cancelConnect() now terminates the link, which makes the blocking
// call return at once.
void testCancelDuringSecurityUnblocksPromptly() {
  init();
  Furble::Host::FujifilmVirtualCamera inner(secureConfig());
  // Block for 10 s, two orders of magnitude above the bound asserted below, so
  // a cancel that does not reach the blocking call fails deterministically
  // instead of hiding inside a short timeout.
  Furble::Host::SecureTimeoutPeer peer(inner, 520, 10000);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = inner.advertisement();

  Furble::FujifilmSecure camera(&advertisement);

  std::atomic<bool> finished {false};
  std::thread connector([&]() {
    (void)camera.connect(ESP_PWR_LVL_P3, 1000);
    finished = true;
  });

  // Let the attempt reach the security wait, then cancel the way an
  // interactive disconnect does. The link coming up is the sync point; a fixed
  // sleep can land before the wait is entered on a loaded runner.
  check(waitForLink(inner, 5000), "the attempt reaches the security wait");
  const auto cancelled = std::chrono::steady_clock::now();
  camera.cancelConnect();
  // Control::disconnect() sets the token under its mutex and wakes the blocked
  // call after releasing it. Drive both halves the same way here.
  camera.abortBlockingConnect();
  connector.join();

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - cancelled)
                           .count();
  check(finished.load(), "the cancelled attempt returns");
  check(elapsed < 1000, "the security wait aborts within 1 s of the cancel");
  if (elapsed >= 1000) {
    std::cerr << "  (took " << elapsed << " ms)\n";
  }
  check(!camera.isConnected(), "the cancelled attempt leaves the camera disconnected");
}

// Reset semantics 1 of 3: a completed handshake ends the run.
//
// SECURE_FAILURE_LIMIT is two because one failed handshake is indistinguishable
// from a lost pairing PDU. That argument only holds if the two failures are
// consecutive. Without the reset on success, a failure, a healthy session and a
// later unrelated failure add up to the limit, and a good bond is deleted on
// what is really a single failure.
void testSuccessBetweenFailuresIsNotARun() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(1);
  // Once the camera has paired, every further attempt scans for it first, so
  // the stub has to answer the way the advertising camera does on the bench.
  Furble::Host::setScanAdvertisement(&advertisement);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first transient timeout fails its attempt");
  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the camera then connects normally");
  camera.disconnect();

  // Much later, an unrelated transient. It is the first failure of a new run,
  // not the second of the old one.
  peer.setSecureTimeouts(1);
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "a later transient timeout fails its attempt");
  check(NimBLEDevice::deleteBondCount() == 0,
        "a success between two failures is not a run, so the bond survives");
  check(NimBLEDevice::isBonded(camera.getAddress()), "the healthy bond is still there");
  check(!camera.needsRepair(), "and no re-pair prompt is raised");
  Furble::Host::setScanAdvertisement(nullptr);
}

// Reset semantics 2 of 3: a user cancel is not a failed handshake.
//
// Bench steps 4 and 5 are exactly this: the user cancels during a security wait
// and then cancels again. The handshake did fail, but because the user stopped
// it, not because the keys are dead. Counting those would delete a healthy bond
// on the second cancel and raise "Pairing lost" about a camera that never lost
// anything.
void testCancelledSecurityWaitIsNotAFailure() {
  init();
  Furble::Host::FujifilmVirtualCamera inner(secureConfig());
  Furble::Host::SecureTimeoutPeer peer(inner, 520, 10000);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = inner.advertisement();

  // Bonded, so the recovery is armed: this is the state in which a miscounted
  // cancel costs the user a re-pair.
  NimBLEDevice::setBonded(true);

  Furble::FujifilmSecure camera(&advertisement);

  for (int attempt = 0; attempt < 2; attempt++) {
    camera.clearConnectCancel();
    std::thread connector([&]() { (void)camera.connect(ESP_PWR_LVL_P3, 1000); });
    check(waitForLink(inner, 5000), "the attempt reaches the security wait");
    camera.cancelConnect();
    camera.abortBlockingConnect();
    connector.join();
  }

  check(NimBLEDevice::deleteBondCount() == 0, "two user cancels never delete a healthy bond");
  check(NimBLEDevice::isBonded(camera.getAddress()), "the bond survives both cancels");
  check(!camera.needsRepair(), "a cancelled wait raises no re-pair prompt");
}

// Reset semantics 3 of 3: an unbonded attempt clears the run.
//
// A fresh pairing replaces the keys the run was measured against, so anything
// counted before it is meaningless. Without the reset a leftover count of one
// survives the re-pair and the very next single failure deletes the brand new
// bond.
void testUnbondedAttemptClearsTheRun() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(Furble::Host::FujifilmVirtualCamera::kSecureTimeoutAlways);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the bonded attempt fails and opens a run");
  check(NimBLEDevice::deleteBondCount() == 0, "one failure keeps the bond");

  // The user forgot the camera and started over, so the next attempt has no
  // bond at all.
  NimBLEDevice::setBonded(false);
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the unbonded attempt fails too");

  // The re-pair took, and the first failure against the new keys arrives.
  NimBLEDevice::setBonded(true);
  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first failure after the re-pair fails");
  check(NimBLEDevice::deleteBondCount() == 0,
        "the unbonded attempt cleared the run, so the fresh bond survives one failure");
  check(NimBLEDevice::isBonded(camera.getAddress()), "the fresh bond is still there");
  check(!camera.needsRepair(), "and no re-pair prompt is raised");
}

// Reset semantics 4 of 4: the run does not span connect cycles.
//
// resetConnectionState() runs on every fresh user connect request, from
// Control::addActive(), and already clears the re-pair flag. The failure run
// has to go with it. Otherwise "two consecutive failures" spans days: one
// transient timeout today, the user gives up, one transient timeout next week,
// and a healthy bond is deleted behind a "Pairing lost" box that is wrong until
// the moment it deletes the bond. Every piece of hardware evidence for the
// recovery is back to back failures inside one reconnect cycle.
void testResetConnectionStateClearsTheRun() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(Furble::Host::FujifilmVirtualCamera::kSecureTimeoutAlways);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first attempt fails and opens a run");
  check(NimBLEDevice::deleteBondCount() == 0, "one failure keeps the bond");

  // The user gave up and came back later. This is what a fresh connect request
  // does to the camera before the attempt starts.
  camera.resetConnectionState();

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first failure of the new cycle fails too");
  check(NimBLEDevice::deleteBondCount() == 0,
        "the run does not span connect cycles, so the bond survives");
  check(NimBLEDevice::isBonded(camera.getAddress()), "the healthy bond is still there");
  check(!camera.needsRepair(), "and no re-pair prompt is raised");
}

}  // namespace

int main() {
  testTimeoutRunDeletesBondAndFlagsRepair();
  testEventPendingTimeoutRunReachesTheSameVerdict();
  testSingleTimeoutThenSuccessKeepsBond();
  testInLinkFreshPairProceedsToRegistration();
  testUnbondedRefusalDeletesNothing();
  testCancelDuringSecurityUnblocksPromptly();
  testSuccessBetweenFailuresIsNotARun();
  testCancelledSecurityWaitIsNotAFailure();
  testUnbondedAttemptClearsTheRun();
  testResetConnectionStateClearsTheRun();
  NimBLEDevice::resetMock();
  if (failures != 0) {
    std::cerr << failures << " stale-bond checks failed\n";
    return 1;
  }
  std::cout << "fujifilm stale bond: PASS\n";
  return 0;
}
