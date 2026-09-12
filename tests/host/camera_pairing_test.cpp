// The pairing request state machine, driven through the publish seam with no
// live link. The peer test covers the same state machine over a real handshake;
// this one covers the guards that answer a request without ever reaching the
// UI: malformed codes, duplicates, and an unknown prompt type.
#include <cstdint>
#include <iostream>

#include "Camera.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

// The window must not outlive the SMP procedure it belongs to. NimBLE gives up
// after BLE_SM_TIMEOUT_MS, 30000 in
// components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c, so a longer window
// would leave a code on screen that can no longer authorize anything.
static_assert(Furble::Camera::PAIRING_WINDOW_MS <= 30000,
              "the pairing window must not exceed the SMP timeout");

namespace {

Furble::Camera *lastRequest = nullptr;
uint32_t requestCount = 0;
bool acceptRequests = true;

bool pairingCallback(Furble::Camera *camera) {
  lastRequest = camera;
  requestCount++;
  return acceptRequests;
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
  NimBLEDevice::resetMock();
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

  // A second request while one is on screen must be refused at the wire, not
  // just dropped. Leaving the peer's second comparison unanswered would hold
  // its SMP procedure open behind a code the user is not looking at.
  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 111111);
  if (!check(requestCount == 1, "a duplicate request does not publish a second prompt")
      || !check(camera.getPairingCode() == 428913,
                "a duplicate request cannot replace the displayed code")
      || !check(NimBLEDevice::mockPasskeyConfirmCount() == 1,
                "a duplicate request is answered exactly once")
      || !check(!NimBLEDevice::mockLastPasskeyAccept(), "a duplicate request is rejected")) {
    return 1;
  }

  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 1000000);
  if (!check(requestCount == 1, "an out-of-range code is rejected")
      || !check(camera.getPairingCode() == 428913,
                "a malformed request leaves the valid request untouched")
      || !check(NimBLEDevice::mockPasskeyConfirmCount() == 2, "a malformed request is answered too")
      || !check(!NimBLEDevice::mockLastPasskeyAccept(), "a malformed request is rejected")) {
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

  // A handler that cannot take the request, which is what a full UI request
  // queue reports, must not leave it pending with nothing to expire it, and it
  // must be answered with a reject rather than the headless accept: the prompt
  // was never drawn, so nobody compared the code.
  acceptRequests = false;
  const uint32_t before = requestCount;
  const size_t answersBefore = NimBLEDevice::mockPasskeyConfirmCount();
  camera.hostSetPairingRequest(Furble::Camera::PairingType::NUMERIC_COMPARISON, 246810);
  if (!check(requestCount == before + 1, "a declined request still reaches the handler")
      || !check(!camera.hasPendingPairing(), "a declined request is answered, not stranded")) {
    return 1;
  }
  acceptRequests = true;
  // No client here, so nothing reaches NimBLE. camera_pairing_peer_test.cpp
  // covers the injected reject over a live link; this only pins that the
  // decline path does not quietly inject an accept from the state machine.
  if (!check(NimBLEDevice::mockPasskeyConfirmCount() == answersBefore,
             "a declined request with no link injects nothing")) {
    return 1;
  }

  Furble::Camera::setPairingRequestCallback(nullptr);
  NimBLEDevice::resetMock();
  std::cout << "camera pairing state: PASS\n";
  return 0;
}
