#include <cstdint>
#include <iostream>

#include "Camera.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"

const char *LOG_TAG = "furble-host";

namespace {

Furble::Camera *lastRequest = nullptr;
uint32_t requestCount = 0;

void pairingCallback(Furble::Camera *camera) {
  lastRequest = camera;
  requestCount++;
}

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  Furble::Host::FujifilmVirtualCamera peer;
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Furble::FujifilmBasic camera(&advertisement);
  Furble::Camera::setPairingRequestCallback(pairingCallback);

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 428913);
  if (!check(lastRequest == &camera, "the callback receives the requesting camera")
      || !check(requestCount == 1, "the first request is published once")
      || !check(camera.hasPendingPairing(), "the numeric request is pending")
      || !check(camera.getPairingType() == Furble::Camera::PairingType::NUMERIC_COMPARISON,
                "the numeric request type is retained")
      || !check(camera.getPairingCode() == 428913, "the numeric code is retained")
      || !check(!camera.pairingTimedOut(), "a fresh request is not timed out")) {
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 111111);
  if (!check(requestCount == 1, "a duplicate request does not publish a second prompt")
      || !check(camera.getPairingCode() == 428913,
                "a duplicate request cannot replace the displayed code")) {
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 1000000);
  if (!check(requestCount == 1, "an out-of-range code is rejected")
      || !check(camera.getPairingCode() == 428913,
                "a malformed request leaves the valid request untouched")) {
    return 1;
  }

  if (!check(camera.answerPairing(true), "the valid numeric request can be accepted")
      || !check(!camera.hasPendingPairing(), "acceptance clears the pending request")) {
    return 1;
  }

  camera.hostSetPairingRequest(static_cast<Furble::Camera::PairingType>(0xff), 123456);
  if (!check(!camera.hasPendingPairing(), "an unknown pairing type is rejected")) {
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::PASSKEY_DISPLAY, 654321);
  if (!check(requestCount == 2, "a later display request is published")
      || !check(camera.getPairingType() == Furble::Camera::PairingType::PASSKEY_DISPLAY,
                "the display request type is retained")) {
    return 1;
  }
  camera.cancelPairing();
  if (!check(!camera.hasPendingPairing(), "cancellation clears a display request")) {
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 314159);
  camera.hostExpirePairing();
  if (!check(camera.pairingTimedOut(), "the host seam can deterministically expire a request")
      || !check(camera.answerPairing(true), "an expired request is consumed")
      || !check(!camera.hasPendingPairing(), "expiry acceptance cannot leave stale state")) {
    return 1;
  }

  Furble::Camera::setPairingRequestCallback(nullptr);
  std::cout << "camera pairing state: PASS\n";
  return 0;
}
