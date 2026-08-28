// Host regression tests for the Fujifilm app-level registration gate.
//
// NimBLE link establishment and ATT acknowledgements are deliberately not
// treated as registration. The virtual peer can answer every GATT operation,
// withhold CHR_NOT1_UUID, and replay a callback from a previous session.

#include <chrono>
#include <iostream>
#include <thread>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-registration-gate-test";

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

void testBasicConfirmationAndReset() {
  init();
  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmBasic camera(&advertisement);

  check(camera.connect(ESP_PWR_LVL_P3, 1000),
        "Basic connect requires and receives registration confirmation");
  check(peer.configured(), "Basic peer records the 01 00 registration notification");
  camera.disconnect();

  // A later reconnect must clear the old confirmation. Replay the callback
  // retained from the prior NimBLE client while the new attempt is waiting.
  peer.setWithholdRegistration(true);
  bool connected = true;
  std::thread reconnect([&]() { connected = camera.connect(ESP_PWR_LVL_P3, 1000); });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  check(peer.emitStaleRegistration(), "virtual peer can replay a stale callback");
  reconnect.join();
  check(!connected, "stale registration callback cannot confirm a reconnect");
  check(!camera.isConnected() && !peer.connected(),
        "Basic timeout tears down the link after registration is withheld");
}

void testSecureConfirmationAndTimeout() {
  init();
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);

  check(camera.connect(ESP_PWR_LVL_P3, 1000),
        "Secure connect requires and receives X100VI registration confirmation");
  check(peer.configured(), "Secure peer records the captured 01 00 notification");
  camera.disconnect();

  peer.setWithholdRegistration(true);
  // Use a fresh camera object so this is a new-pair attempt. A saved Secure
  // reconnect intentionally scans first, which is a separate lifecycle path.
  Furble::FujifilmSecure withheld(&advertisement);
  const auto started = std::chrono::steady_clock::now();
  check(!withheld.connect(ESP_PWR_LVL_P3, 1000),
        "Secure link-only connect fails when registration is withheld");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  check(elapsed.count() < 1000, "registration timeout remains bounded in the host test");
}

}  // namespace

int main() {
  testBasicConfirmationAndReset();
  testSecureConfirmationAndTimeout();
  NimBLEDevice::resetMock();
  if (failures != 0) {
    std::cerr << failures << " registration-gate checks failed\n";
    return 1;
  }
  std::cout << "fujifilm registration gate: PASS\n";
  return 0;
}
