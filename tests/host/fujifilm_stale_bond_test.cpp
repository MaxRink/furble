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

// Two limits of the mock this scenario runs against, so neither is mistaken for
// tested behaviour.
//
// FujifilmSecure warns "No identity address on the live link" when the bond
// snapshot cannot be read. That branch is unreachable here: MockNimBLE's
// getIdAddress() falls back to the link's own peer, which a live link always
// has, so it never returns the empty address the warning tests for. On hardware
// the read can fail, which is why the warning exists.
//
// And the mock keeps one link peer address globally, not one per client. Every
// scenario in this file drives a single camera, so that is enough; a test that
// wanted two simultaneous unresolved links would need it per client.
//
// The camera deleted its pairing but is sitting in pairing mode: it refuses the
// dead keys while staying on the link. Deleting the stale bond then terminates
// that link, because an unpair does, so the in-link fresh pair cannot run and
// the user is prompted instead. The pairing they are asked for succeeds on the
// next attempt.
void testBondDeleteDropsTheLinkAndPrompts() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  NimBLEDevice::setBonded(true);
  peer.setRefuseWhileBonded(true);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first refusal is not yet evidence");
  check(NimBLEDevice::deleteBondCount() == 0, "one refusal keeps the bond");

  // The bond delete takes the link with it, so the in-link fresh pair has
  // nothing to pair on and the user gets the prompt. That is the hardware
  // outcome: NimBLEDevice::deleteBond() is ble_gap_unpair(), which terminates
  // every connection to the peer before dropping the keys. The mock modelled
  // only the key drop until this was fixed, which is why this scenario used to
  // assert a recovery the device cannot perform.
  check(!camera.connect(ESP_PWR_LVL_P3, 1000),
        "the second refusal deletes the bond, and the unpair drops the link with it");
  check(NimBLEDevice::deleteBondCount() == 1, "the recovery deletes the bond exactly once");
  check(camera.needsRepair(), "so the user is asked to re-pair rather than silently recovering");
  check(!peer.configured(), "and no registration happened on a link that no longer exists");

  // The pairing the user is being asked for. The bond is gone, the camera is
  // still in its pairing screen, so the next attempt is a first pairing and it
  // completes.
  peer.setRefuseWhileBonded(false);
  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the next attempt pairs fresh and connects");
  check(peer.configured(), "and completes registration");
  check(NimBLEDevice::deleteBondCount() == 1, "with no further bond deletes");
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

// The 2026-09-05 bench, replayed. The recovery never fired on real hardware and
// the reason was not the counter logic: it was where the bond is looked up.
//
// A Fujifilm Secure body advertises a resolvable private address, and the bond
// store files bonds under the identity address. `isBonded(advertised)`
// therefore answers false on every saved reconnect even with the bond in place,
// so every attempt took the "nothing stale to measure" early return, cleared
// the run on the way in, and never counted a failure at all. Eight reconnects,
// rc=13 and rc=520 throughout, and not one "Security handshake failed (1 of 2)"
// line in the log.
//
// Here the advertised address and the identity address are deliberately
// different, which is the whole point: read the bond by the advertised one and
// this test fails exactly as the bench did.
void testRotatingAddressFindsTheBondAndRecovers() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  const NimBLEAddress identity(0x998877665544ULL, 0);
  NimBLEDevice::setMockIdAddress(identity);
  NimBLEDevice::setBondedAddress(identity);
  check(!NimBLEDevice::isBonded(peer.config().address),
        "the advertised address is not what the bond is filed under");
  check(NimBLEDevice::isBonded(identity), "the identity address is");

  // The bench shapes, in order: rc=13 then rc=520, both after "Connected" and
  // "Securing". The third link-up finds the camera in its pairing screen and
  // the fresh in-link pair goes through.
  peer.setSecureTimeouts(2);

  Furble::FujifilmSecure camera(&advertisement);

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the first failure is not yet evidence");
  check(NimBLEDevice::deleteBondCount() == 0, "and leaves the bond alone");

  check(!camera.connect(ESP_PWR_LVL_P3, 1000), "the second failure fails its attempt too");
  check(NimBLEDevice::deleteBondCount() == 1, "but it deletes the stale bond, exactly once");
  check(!NimBLEDevice::isBonded(identity), "and it is the identity bond that went");
  // rc=13 and rc=520 both take the link with them, so there is no live link
  // left to pair on: the in-link fast path is for a camera that refuses the
  // dead keys while staying connected, which is a different shape and has its
  // own scenario above. Here the user gets the prompt.
  check(camera.needsRepair(), "and the user is asked to re-pair");

  // The third link-up. The bond is gone, the camera is in its pairing screen,
  // so this is a first pairing and it has to go all the way through
  // registration. On the bench this is the attempt that never came, because the
  // two before it were never counted.
  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the next attempt pairs fresh and connects");
  check(peer.configured(), "and completes registration");
  check(NimBLEDevice::deleteBondCount() == 1, "with no further bond deletes");
  camera.disconnect();
}

// A successful connect establishes security once. A second initiate on a link
// that is already encrypted is answered rc=2 by the stack and terminated by the
// camera, which is how the 2026-09-05 bench lost every session that did manage
// to pair: "Secured!", "Requesting status", "ble_gap_security_initiate: rc=2",
// "Disconnected". The library-side retry that issued it is fixed in
// components/esp-nimble-cpp (NimBLEClient::secureConnection now reports success
// without re-initiating when the link is already encrypted); this pins the half
// that lives in furble, that nothing here asks for security twice.
void testSuccessfulConnectSecuresExactlyOnce() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  Furble::FujifilmSecure camera(&advertisement);
  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the camera connects");
  check(peer.configured(), "and completes registration");
  check(peer.secureInitiateCount() == 1,
        "security is established exactly once, never again after Secured!");
  camera.disconnect();
}

// A camera that answers everything and confirms registration late.
//
// Hardware fact this encodes, from the 2026-09-04 console capture on a bonded
// X100VI: a reconnect reaches "Identifying" at +13 s and progress 85 at +25 s,
// but the camera's registration notifications only arrive around +30 to +45 s.
// REGISTRATION_TIMEOUT_MS is 25 s, so on that body the first attempt after a
// reconnect can legitimately time out and a later retry is what completes. A
// slow confirmation is therefore not a fault to be recovered from, and in
// particular it is not a security failure: it must not touch the bond, must not
// count toward the stale-bond run, and above all the wait must end on its own
// rather than parking the attempt.
//
// That last point is what this catches. The wait picks its clock by asking
// whether a FreeRTOS tick exists; picking by platform macro instead sent the
// simulator, which defines no platform macro, onto the wall clock while its
// scenarios advance a virtual one, and the attempt then neither confirmed nor
// timed out.
void testLateRegistrationTimesOutThenRecovers() {
  init();
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  Furble::FujifilmSecure camera(&advertisement);

  // Slower to confirm than the registration timeout allows.
  peer.setWithholdRegistration(true);
  check(!camera.connect(ESP_PWR_LVL_P3, 1000),
        "an attempt whose confirmation never lands fails on its own timeout");
  check(!peer.configured(), "and never reports itself registered");
  check(NimBLEDevice::deleteBondCount() == 0, "a slow confirmation is not a security failure");
  check(!camera.needsRepair(), "and never asks the user to re-pair");

  // The confirmation arrives, which on the bench is the later retry completing.
  peer.setWithholdRegistration(false);
  check(camera.connect(ESP_PWR_LVL_P3, 1000), "the retry once the camera confirms connects");
  check(peer.configured(), "and completes registration");
  check(NimBLEDevice::deleteBondCount() == 0, "with the bond still untouched throughout");
  camera.disconnect();
}

}  // namespace

int main() {
  testTimeoutRunDeletesBondAndFlagsRepair();
  testEventPendingTimeoutRunReachesTheSameVerdict();
  testSingleTimeoutThenSuccessKeepsBond();
  testBondDeleteDropsTheLinkAndPrompts();
  testUnbondedRefusalDeletesNothing();
  testCancelDuringSecurityUnblocksPromptly();
  testSuccessBetweenFailuresIsNotARun();
  testCancelledSecurityWaitIsNotAFailure();
  testUnbondedAttemptClearsTheRun();
  testResetConnectionStateClearsTheRun();
  testRotatingAddressFindsTheBondAndRecovers();
  testSuccessfulConnectSecuresExactlyOnce();
  testLateRegistrationTimesOutThenRecovers();
  NimBLEDevice::resetMock();
  if (failures != 0) {
    std::cerr << failures << " stale-bond checks failed\n";
    return 1;
  }
  std::cout << "fujifilm stale bond: PASS\n";
  return 0;
}
