// Ricoh standby flap through the REAL Control, the first Ricoh-through-Control
// coverage in the repo.
//
// Hardware scenario (2026-08-28 incident): a GR IV in BLE standby together
// with a healthy X100VI. The GR IV accepts the BLE connect but fails
// secureConnection() with rc=520 on some attempts (the link dies under the
// encryption handshake with the disconnect event still queued), sometimes
// completes the handshake and then drops the link ~20 s later after a
// CameraPower 0x00 notify. While the reconnect cycle churns against the GR IV
// the user taps disconnect mid-cycle. The wedge on the reverted PR #159 build
// left the state machine in DISCONNECTING; current master must return to a
// clean IDLE, publish no late state, and accept a working follow-up connect.
//
// The RicohVirtualCamera standby-flap mode drives the churn autonomously:
// setFlappy(1, 1000) fails one secureConnection per cycle (rc=520 class via
// mockMarkLinkDeadEventPending), completes the next, then emits CameraPower
// 0x00 and severs the link one second later (the time-compressed standby
// drop; the delay leaves the both-live assertions a comfortable window).

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

const char *LOG_TAG = "furble-ricoh-control-flap";

namespace {

using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;
using Furble::Host::RicohVirtualCamera;

int g_Failures = 0;

bool check(bool condition, const char *message) {
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

bool waitForState(Control::state_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (control.getState() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() == want;
}

bool waitForNotState(Control::state_t avoid, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (control.getState() != avoid) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() != avoid;
}

bool waitForConnectedCount(size_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while (Furble::Host::timeoutPending(start, nowMs(), timeout_ms)) {
    if (control.getConnectedTargetCount() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getConnectedTargetCount() == want;
}

bool powerOffNotifySeen(const RicohVirtualCamera &peer) {
  for (const auto &notification : peer.notifications()) {
    if ((notification.characteristic == RicohVirtualCamera::powerCharacteristicUUID().toString())
        && (notification.payload.size() == 1) && (notification.payload[0] == 0x00)) {
      return true;
    }
  }
  return false;
}

bool runScenario() {
  auto &control = Control::getInstance();

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  FujifilmVirtualCamera::Config goodConfig;
  goodConfig.name = "FUJIFILM X100VI";
  goodConfig.address = NimBLEAddress(0x112233445501ULL, 0);
  goodConfig.token = {0x11, 0x22, 0x33, 0x44};
  FujifilmVirtualCamera good(goodConfig);

  RicohVirtualCamera::Config flapConfig;
  flapConfig.name = "RICOH GR IV";
  flapConfig.address = NimBLEAddress(0x3490EABB7D73ULL, 0);
  RicohVirtualCamera flap(flapConfig);
  flap.setFlappy(/*fail_attempts=*/1, /*drop_after_ms=*/1000);

  NimBLEDevice::setMockPeerForAddress(good.advertisement().getAddress(), &good);
  NimBLEDevice::setMockPeerForAddress(flap.advertisement().getAddress(), &flap);

  // Both cameras are bonded. Ricoh clears a stale local bond on a failed
  // live-scan pairing by design (an explicit request for a new pairing), so
  // this scenario only asserts that the Fujifilm Secure re-pair verdict never
  // reaches a Ricoh. The saved-reconnect bond invariant is asserted below.
  NimBLEDevice::setBonded(true);

  const NimBLEAdvertisedDevice goodAdvertisement = good.advertisement();
  const NimBLEAdvertisedDevice flapAdvertisement = flap.advertisement();
  auto goodCamera = std::make_shared<Furble::FujifilmBasic>(&goodAdvertisement);
  auto flapCamera = std::make_shared<Furble::Ricoh>(&flapAdvertisement);

  control.addActive(goodCamera);
  control.addActive(flapCamera);
  check(control.getTargetCount() == 2, "two targets selected");

  // Phase A: the GR IV fails secureConnection once with rc=520 (client left
  // reporting connected with the disconnect event queued), the retry then
  // completes the handshake, so the session reaches active.
  control.connectAll(true);
  check(waitForState(Control::STATE_ACTIVE, 8000),
        "both cameras reach active despite the rc=520 churn");
  check(waitForConnectedCount(2, 1000), "both links live before the standby drop");

  // Phase B: the standby drop fires on the peer's own timer: CameraPower 0x00
  // notify first, then the link severs. Control leaves active and churns.
  check(waitForNotState(Control::STATE_ACTIVE, 3000), "the GR IV drops on its own");
  check(good.connected(), "the healthy camera keeps its link through the churn");
  check(powerOffNotifySeen(flap), "the CameraPower 0x00 notify preceded the drop");

  // Phase C: disconnect mid-cycle, wherever the churn happens to be.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const uint32_t start = nowMs();
  const bool completed = control.disconnect();
  const uint32_t elapsed = nowMs() - start;
  check(completed, "mid-cycle disconnect completes");
  check(elapsed < 3000, "mid-cycle disconnect returns within 3 s");
  check(waitForState(Control::STATE_IDLE, 3000),
        "state machine returns to idle, not wedged in disconnecting");
  check(control.getTargetCount() == 0, "targets cleared");
  check(!flapCamera->needsRepair(), "a standby camera is never flagged for a re-pair");
  check(!goodCamera->needsRepair(), "the healthy camera is never flagged for a re-pair");

  // A late republish of CONNECT or DISCONNECTING here is the wedge class.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  check(control.getState() == Control::STATE_IDLE, "no late state republish after the churn");

  // Phase D: a follow-up connect must work. Disable the flap first (joins the
  // drop timer) so resetMock cannot free a client the timer still references.
  flap.setFlappy(0, 0);
  NimBLEDevice::resetMock();
  FujifilmVirtualCamera fresh(goodConfig);
  NimBLEDevice::setMockPeer(&fresh);
  const NimBLEAdvertisedDevice freshAdvertisement = fresh.advertisement();
  auto freshCamera = std::make_shared<Furble::FujifilmBasic>(&freshAdvertisement);
  control.addActive(freshCamera);
  const uint32_t reconnectStart = nowMs();
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 5000), "a follow-up connect reaches active");
  check(nowMs() - reconnectStart < 4000, "the follow-up connect is bounded");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);
  return g_Failures == 0;
}

/**
 * A SAVED GR IV must keep its pairing across the standby flap.
 *
 * The flap fails secureConnection() with the rc=520 shape (the failure reaches
 * the connect task with the disconnect event still queued), which is exactly
 * the shape the Fujifilm Secure stale-bond recovery acts on after a run of
 * two. That recovery is vendor local and must stay that way: a GR IV in
 * standby comes back on its own, so losing its bond would cost the user a
 * re-pair for an entirely normal power state. Ricoh's own bond clear is scoped
 * to PairType::NEW, a live-scan pairing, and must not reach a saved reconnect.
 */
bool runSavedBondScenario() {
  auto &control = Control::getInstance();

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);

  RicohVirtualCamera::Config flapConfig;
  flapConfig.name = "RICOH GR IV";
  flapConfig.address = NimBLEAddress(0x3490EABB7D74ULL, 0);
  RicohVirtualCamera flap(flapConfig);
  // Fail two consecutive handshakes, the run length that would trip the
  // Fujifilm Secure recovery, then complete one and drop the link again.
  flap.setFlappy(/*fail_attempts=*/2, /*drop_after_ms=*/1000);

  const NimBLEAdvertisedDevice flapAdvertisement = flap.advertisement();
  NimBLEDevice::setMockPeerForAddress(flapAdvertisement.getAddress(), &flap);
  NimBLEDevice::setBonded(true);

  // Rebuild through the NVS record so the camera is PairType::SAVED, the
  // reconnect path a bonded camera actually takes.
  Furble::Ricoh scanned(&flapAdvertisement);
  std::vector<uint8_t> nvs(scanned.getSerialisedBytes());
  check(scanned.serialise(nvs.data(), nvs.size()), "the Ricoh NVS record round-trips");
  auto savedCamera = std::make_shared<Furble::Ricoh>(nvs.data(), nvs.size());

  control.addActive(savedCamera);
  control.connectAll(true);

  // Let the reconnect cycle churn through several failed handshakes.
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  check(NimBLEDevice::deleteBondCount() == 0,
        "a saved Ricoh keeps its bond across repeated standby handshake failures");
  check(NimBLEDevice::isBonded(savedCamera->getAddress()), "the GR IV pairing survives");
  check(!savedCamera->needsRepair(), "a standby Ricoh is never flagged for a re-pair");
  check(control.getState() != Control::STATE_CONNECT_FAILED,
        "the Ricoh reconnect keeps retrying instead of stopping with a re-pair prompt");

  flap.setFlappy(0, 0);
  control.disconnect();
  waitForState(Control::STATE_IDLE, 3000);
  return g_Failures == 0;
}

}  // namespace

int main() {
  // Hard watchdog: a wedged control task must fail the test, not hang it.
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(60));
    std::cerr << "FAIL: watchdog timeout, control never settled\n";
    std::_Exit(2);
  }).detach();

  FurbleHostTaskScope taskScope;
  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  const bool ok = runScenario() && runSavedBondScenario();
  if (!ok || g_Failures != 0) {
    std::cout << "ricoh control flap: FAIL (" << g_Failures << " checks)\n";
    return EXIT_FAILURE;
  }
  std::cout << "ricoh control flap: PASS\n";
  return EXIT_SUCCESS;
}
