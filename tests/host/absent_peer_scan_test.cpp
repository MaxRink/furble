// Absent-peer model through the REAL Control: a saved camera whose address is
// registered but which never appears in scan results.
//
// This is distinct from setConnectShouldFail. A paired Fujifilm Secure camera
// reconnects scan-first: FujifilmSecure::_connect starts a SAVED scan and
// waits up to 60 s for the camera's advertisement before NimBLEClient::connect
// is ever reached. A camera that is powered off or out of range therefore
// starves the scan wait, it does not fail the connect call. The mock hook
// NimBLEDevice::setScanAbsentAddress makes the scan withhold that address
// while a background advertiser keeps emitting it, proving the starvation is
// the filter, not test omission.
//
// The user workflow: two saved targets, one absent (the camera left in the
// bag) and one healthy. Connect starts, the absent camera's scan runs, the
// user taps disconnect mid-scan. The cancel poll inside the scan wait must
// abort the attempt within one poll so the disconnect is bounded and the
// machine returns to a clean IDLE, and a manual follow-up connect to the
// healthy camera must respond promptly. Removing the connect-cancel poll from
// the scan wait (the plan 148 wedge class) fails this test.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

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

// LOG_TAG is defined by lib/furble/Scan.cpp, which this target compiles.

namespace Furble {
// Scan is the production implementation. This seam only supplies the discovery
// list matcher, which this saved-reconnect test never exercises.
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

bool runScenario() {
  auto &control = Control::getInstance();

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  // The absent camera: a Fujifilm Secure paired once (pairing path, no scan)
  // and disconnected, so its next connect runs the SAVED scan-first path.
  FujifilmVirtualCamera::Config absentConfig;
  absentConfig.name = "FUJIFILM X100VI ABSENT";
  absentConfig.secure = true;
  absentConfig.address = NimBLEAddress(0x112233445502ULL, 0);
  FujifilmVirtualCamera absentPeer(absentConfig);
  NimBLEDevice::setMockPeerForAddress(absentPeer.advertisement().getAddress(), &absentPeer);
  NimBLEAdvertisedDevice absentAdvertisement = absentPeer.advertisement();
  auto absentCamera = std::make_shared<Furble::FujifilmSecure>(&absentAdvertisement);
  check(absentCamera->connect(ESP_PWR_LVL_P3, 1000), "initial pairing connect succeeds");
  absentCamera->disconnect();

  // Now the camera is gone: registered, but its advertisement is never
  // delivered even while the advertiser pump keeps emitting it.
  NimBLEDevice::setScanAbsentAddress(absentAdvertisement.getAddress(), true);
  std::atomic<bool> stopAdvertiser {false};
  std::thread advertiser([&]() {
    while (!stopAdvertiser.load()) {
      NimBLEDevice::getScan()->emitResult(&absentAdvertisement);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // The healthy camera.
  FujifilmVirtualCamera::Config goodConfig;
  goodConfig.name = "FUJIFILM X100VI";
  goodConfig.address = NimBLEAddress(0x112233445501ULL, 0);
  goodConfig.token = {0x11, 0x22, 0x33, 0x44};
  FujifilmVirtualCamera goodPeer(goodConfig);
  NimBLEDevice::setMockPeerForAddress(goodPeer.advertisement().getAddress(), &goodPeer);
  const NimBLEAdvertisedDevice goodAdvertisement = goodPeer.advertisement();
  auto goodCamera = std::make_shared<Furble::FujifilmBasic>(&goodAdvertisement);

  // The absent camera is first in selection order, so connectAll blocks in its
  // saved scan and the disconnect below lands mid-scan.
  control.addActive(absentCamera);
  control.addActive(goodCamera);
  check(control.getTargetCount() == 2, "two targets selected");
  control.connectAll(true);

  // Mid-scan: the scan wait has started and no advertisement will arrive.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  check(control.getState() == Control::STATE_CONNECTING, "connect is parked in the saved scan");

  const uint32_t start = nowMs();
  const bool completed = control.disconnect();
  const uint32_t elapsed = nowMs() - start;
  check(completed, "mid-scan disconnect completes");
  check(elapsed < 3000, "mid-scan disconnect returns within 3 s");
  check(waitForState(Control::STATE_IDLE, 3000), "returns to idle after the aborted scan");
  check(control.getTargetCount() == 0, "targets cleared");

  // A late republish here is the wedge class.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  check(control.getState() == Control::STATE_IDLE, "no late state republish after the scan abort");

  // Manual connect responsiveness: the healthy camera must connect promptly,
  // proving the control task is not still parked inside the absent scan.
  control.addActive(goodCamera);
  const uint32_t reconnectStart = nowMs();
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 3000), "manual connect to the healthy camera works");
  check(nowMs() - reconnectStart < 3000, "the manual connect is bounded");

  control.disconnect();
  waitForState(Control::STATE_IDLE, 2000);

  stopAdvertiser.store(true);
  advertiser.join();
  return g_Failures == 0;
}

}  // namespace

int main() {
  // Hard watchdog: a scan wait that ignores the cancel poll must fail the
  // test, not hang the harness for the 60 s scan window.
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    std::cerr << "FAIL: watchdog timeout, the absent-peer scan never resolved\n";
    std::_Exit(2);
  }).detach();

  FurbleHostTaskScope taskScope;
  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  const bool ok = runScenario();
  if (!ok || g_Failures != 0) {
    std::cout << "absent-peer scan: FAIL (" << g_Failures << " checks)\n";
    return EXIT_FAILURE;
  }
  std::cout << "absent-peer scan: PASS\n";
  return EXIT_SUCCESS;
}
