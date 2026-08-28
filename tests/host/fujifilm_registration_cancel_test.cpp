// Host regression test for cancelling a Fujifilm connect during the
// registration wait at the Camera level.
//
// Plan 148 covers the Control-level teardown (control_teardown_wedge_test):
// Control::disconnect() sets the per-camera cancel token and the wait aborts.
// This test pins the Camera-level contract directly: Camera::cancelConnect()
// (the PR #242 token) aborts waitForRegistration() within one poll slice and
// the attempt unwinds cleanly, instead of parking for the full registration
// timeout. This target compiles its own camera sources with a 10 s
// registration timeout, an order of magnitude above the asserted bound, so a
// wait that ignores the token fails the bounded-time check deterministically.

#include <chrono>
#include <iostream>
#include <thread>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-registration-cancel-test";

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

uint32_t nowMs() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

// Drive one connect attempt against a peer that withholds registration, land
// cancelConnect() once the link is up, and assert the attempt aborts bounded.
template <typename CameraType>
void runCancelScenario(Furble::Host::FujifilmVirtualCamera &peer, const char *variant) {
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  peer.setWithholdRegistration(true);

  CameraType camera(&advertisement);
  bool connected = true;
  std::thread attempt([&]() { connected = camera.connect(ESP_PWR_LVL_P3, 1000); });

  // Wait for the link, then give the attempt time to park in the wait proper.
  const uint32_t linkStart = nowMs();
  while ((nowMs() - linkStart) < 5000 && !peer.connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check(peer.connected(), "link is up while registration is withheld");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const uint32_t cancelStart = nowMs();
  camera.cancelConnect();
  attempt.join();
  const uint32_t elapsed = nowMs() - cancelStart;

  check(!connected, "cancelled connect reports failure");
  check(elapsed < 3000, "cancel aborts the wait bounded, not after the registration timeout");
  check(!camera.isConnected(), "cancelled attempt unwinds the camera link state");
  check(!peer.connected(), "cancelled attempt tears the peer link down");
  std::cout << variant << ": cancel abort after " << elapsed << " ms\n";
}

}  // namespace

int main() {
  {
    init();
    Furble::Host::FujifilmVirtualCamera peer;
    runCancelScenario<Furble::FujifilmBasic>(peer, "basic");
  }
  {
    init();
    Furble::Host::FujifilmVirtualCamera::Config config;
    config.secure = true;
    config.name = "FUJIFILM X100VI";
    Furble::Host::FujifilmVirtualCamera peer(config);
    runCancelScenario<Furble::FujifilmSecure>(peer, "secure");
  }
  NimBLEDevice::resetMock();
  if (failures != 0) {
    std::cerr << failures << " registration-cancel checks failed\n";
    return 1;
  }
  std::cout << "fujifilm registration cancel: PASS\n";
  return 0;
}
