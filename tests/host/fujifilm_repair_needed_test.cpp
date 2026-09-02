// Stale-bond recovery for a SAVED Fujifilm Secure camera, driven through the
// REAL Furble::Control reconnect loop.
//
// Hardware scenario (2026-09-02 bench, X100VI, bench-logs/stale-bond-245-run2):
// furble's pairing was deleted on the camera only, so furble kept its saved
// entry and its local bond. The saved reconnect then came up at the link level
// and the encryption handshake failed with rc=13 or rc=520 every single time.
// The reconnect cycle retried forever (reconnect_attempt climbed past 7) and
// the user had no way back to a pairing except deleting the camera in the UI.
//
// The recovery must delete the stale bond exactly once after a run of
// consecutive failures, then stop the cycle with a user-facing reason rather
// than retrying. This harness compiles the real src/FurbleControl.cpp and the
// real saved-reconnect scan path, so both the loop-termination and the
// bond accounting are asserted on production code.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "Scan.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "WrapSafeTime.h"

// LOG_TAG comes from the production Scan.cpp linked into this harness.

namespace Furble {
// Scan is the production implementation. This seam only supplies the discovery
// list matcher, which this test does not exercise.
bool CameraList::match(const NimBLEAdvertisedDevice *) {
  return false;
}
}  // namespace Furble

namespace {

using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;

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

void freshEnvironment() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);
}

/**
 * Keep the saved-reconnect scan fed.
 *
 * A SAVED camera scans for its advertisement before every connect attempt. On
 * the bench the camera is advertising continuously, so the scan matches within
 * a second; here a background thread stands in for that radio.
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

/** Rebuild the camera through its NVS serialisation, so it is PairType::SAVED. */
std::shared_ptr<Furble::FujifilmSecure> makeSavedCamera(const NimBLEAdvertisedDevice &ad) {
  Furble::FujifilmSecure scanned(&ad);
  std::vector<uint8_t> nvs(scanned.getSerialisedBytes());
  if (!scanned.serialise(nvs.data(), nvs.size())) {
    return nullptr;
  }
  return std::make_shared<Furble::FujifilmSecure>(nvs.data(), nvs.size());
}

void resetControl() {
  auto &control = Control::getInstance();
  control.disconnect();
  waitForState(Control::STATE_IDLE, 5000);
}

// (a) The bench signature. A saved, previously bonded camera whose security
// handshake times out every attempt: the bond goes exactly once and the cycle
// stops with a re-pair reason instead of retrying forever.
bool scenarioRepairNeededStopsTheCycle() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Advertiser advertiser(advertisement);

  // furble still holds the bond; the camera no longer does, so every handshake
  // times out and takes the link with it.
  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(FujifilmVirtualCamera::kSecureTimeoutAlways);

  auto camera = makeSavedCamera(advertisement);
  if (!check(camera != nullptr, "saved camera rebuilt from its NVS record")) {
    return false;
  }
  control.addActive(camera);

  // Infinite reconnect: exactly the mode the bench ran in, and the mode that
  // looped forever before this fix.
  control.connectAll(true);

  check(waitForState(Control::STATE_CONNECT_FAILED, 30000),
        "the reconnect cycle stops in connect_failed instead of looping");
  check(control.getState() == Control::STATE_CONNECT_FAILED, "and stays stopped");

  const std::string reason = control.getConnectFailReason();
  check(!reason.empty(), "the failure carries a user-facing reason");
  check(reason.find("pairing mode") != std::string::npos,
        "the reason tells the user to put the camera in pairing mode");
  check(reason.find(config.name) != std::string::npos, "the reason names the camera");

  check(NimBLEDevice::deleteBondCount() == 1, "the stale local bond is deleted exactly once");
  check(!NimBLEDevice::isBonded(camera->getAddress()), "the dead keys are gone");

  // The cycle really is over: nothing moves it back into connecting, and no
  // further bond deletes happen.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  check(control.getState() == Control::STATE_CONNECT_FAILED,
        "no retry re-enters the connect cycle");
  check(NimBLEDevice::deleteBondCount() == 1, "no further bond deletes while stopped");

  resetControl();
  check(control.getState() == Control::STATE_IDLE, "the machine returns to idle");
  return g_Failures == 0;
}

// (b) A single security timeout is radio noise. The next attempt succeeds, so
// the bond must survive and the session must come up normally.
bool scenarioSingleTimeoutKeepsBond() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Advertiser advertiser(advertisement);

  NimBLEDevice::setBonded(true);
  peer.setSecureTimeouts(1);

  auto camera = makeSavedCamera(advertisement);
  if (!check(camera != nullptr, "saved camera rebuilt from its NVS record")) {
    return false;
  }
  control.addActive(camera);
  control.connectAll(true);

  check(waitForState(Control::STATE_ACTIVE, 30000), "the retry after one timeout connects");
  check(NimBLEDevice::deleteBondCount() == 0, "a single security timeout keeps the bond");
  check(NimBLEDevice::isBonded(camera->getAddress()), "the saved bond survives");
  check(control.getConnectFailReason().empty(), "no re-pair prompt for a camera that came back");
  check(!camera->needsRepair(), "the camera is not flagged for a re-pair");

  resetControl();
  return g_Failures == 0;
}

// (c) The camera dropped its pairing but is sitting in pairing mode: it refuses
// the dead keys while staying on the link. After the bond is deleted the fresh
// in-link pairing goes through and the connect proceeds to registration, so the
// user never sees a prompt at all.
bool scenarioInLinkFreshPairRecovers() {
  freshEnvironment();
  auto &control = Control::getInstance();

  FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Advertiser advertiser(advertisement);

  NimBLEDevice::setBonded(true);
  peer.setRefuseWhileBonded(true);

  auto camera = makeSavedCamera(advertisement);
  if (!check(camera != nullptr, "saved camera rebuilt from its NVS record")) {
    return false;
  }
  control.addActive(camera);
  control.connectAll(true);

  check(waitForState(Control::STATE_ACTIVE, 30000),
        "the in-link fresh pair recovers the session without user action");
  check(NimBLEDevice::deleteBondCount() == 1, "the stale bond is deleted exactly once");
  check(peer.configured(), "the fresh pairing proceeded through registration");
  check(control.getConnectFailReason().empty(), "an in-link recovery raises no re-pair prompt");

  resetControl();
  return g_Failures == 0;
}

// (d) A multi-connect session where one saved body lost its pairing and another
// is perfectly healthy.
//
// connectAll() breaks out of the camera loop on the first failure and returns
// STATE_CONNECT_FAILED, and the UI handler for that state calls doDisconnect()
// before it raises the box. The healthy camera therefore loses its live session
// because a different camera lost its pairing.
//
// That is deliberate, and this pins it. Control never tears the healthy link
// down: it leaves the session up, and the reason names only the camera that
// actually lost its pairing. Ending the whole session is the UI decision, taken
// once on a state that is terminal anyway, rather than leaving a half connected
// session behind a modal the user has to clear first.
bool scenarioOneStaleCameraDoesNotBlameTheHealthyOne() {
  freshEnvironment();
  auto &control = Control::getInstance();

  // The healthy body: a Fujifilm Basic camera at its own address, paired live,
  // so it needs neither a bond nor a saved-reconnect scan.
  FujifilmVirtualCamera::Config healthyConfig;
  healthyConfig.name = "FUJIFILM X-T5";
  healthyConfig.address = NimBLEAddress(0x223344556677ULL, 0);
  FujifilmVirtualCamera healthyPeer(healthyConfig);
  const NimBLEAdvertisedDevice healthyAdvertisement = healthyPeer.advertisement();
  NimBLEDevice::setMockPeerForAddress(healthyConfig.address, &healthyPeer);

  // The stale body: the bench X100VI, saved and bonded, whose handshake now
  // times out on every attempt.
  FujifilmVirtualCamera::Config staleConfig;
  staleConfig.secure = true;
  staleConfig.name = "FUJIFILM X100VI";
  FujifilmVirtualCamera stalePeer(staleConfig);
  const NimBLEAdvertisedDevice staleAdvertisement = stalePeer.advertisement();
  NimBLEDevice::setMockPeerForAddress(staleConfig.address, &stalePeer);
  Advertiser advertiser(staleAdvertisement);

  NimBLEDevice::setBonded(true);
  stalePeer.setSecureTimeouts(FujifilmVirtualCamera::kSecureTimeoutAlways);

  auto healthy = std::make_shared<Furble::FujifilmBasic>(&healthyAdvertisement);
  auto stale = makeSavedCamera(staleAdvertisement);
  if (!check(stale != nullptr, "saved camera rebuilt from its NVS record")) {
    return false;
  }

  // The healthy camera is first, so it is connected and live by the time the
  // stale one fails.
  control.addActive(healthy);
  control.addActive(stale);
  control.connectAll(true);

  check(waitForState(Control::STATE_CONNECT_FAILED, 30000),
        "one stale camera ends the cycle for the whole session");
  const std::string reason = control.getConnectFailReason();
  check(reason.find(staleConfig.name) != std::string::npos, "the reason names the stale camera");
  check(reason.find(healthyConfig.name) == std::string::npos,
        "and never blames the healthy camera");
  check(!healthy->needsRepair(), "the healthy camera is not flagged for a re-pair");
  check(NimBLEDevice::deleteBondCount() == 1, "only the stale camera loses a bond");

  // Control leaves the healthy link alone. Ending that session is the UI
  // doDisconnect() on STATE_CONNECT_FAILED, not a side effect of this branch.
  check(healthy->isConnected(), "Control leaves the healthy session up");

  resetControl();
  check(!healthy->isConnected(), "the interactive teardown ends the healthy session too");
  return g_Failures == 0;
}

}  // namespace

int main() {
  // Hard watchdog: a wedged reconnect must fail the run, not hang the harness.
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(180));
    std::cerr << "FAIL: fujifilm repair-needed harness timed out\n";
    std::_Exit(1);
  }).detach();

  // Joins the control task at scope exit, so the process never tears down with
  // a live FreeRTOS shim thread still running.
  FurbleHostTaskScope taskScope;
  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  std::cout << "repair-needed stops the cycle\n";
  scenarioRepairNeededStopsTheCycle();
  std::cout << "single timeout keeps the bond\n";
  scenarioSingleTimeoutKeepsBond();
  std::cout << "in-link fresh pair recovers\n";
  scenarioInLinkFreshPairRecovers();
  std::cout << "one stale camera does not blame the healthy one\n";
  scenarioOneStaleCameraDoesNotBlameTheHealthyOne();

  if (g_Failures != 0) {
    std::cerr << g_Failures << " repair-needed checks failed\n";
    return 1;
  }
  std::cout << "fujifilm repair needed: PASS\n";
  return 0;
}
