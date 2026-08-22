// AddressSanitizer regression for the mid-connect peer-drop use-after-free.
//
// The crash, found on hardware: while furble was connecting to a Fujifilm
// X100VI the camera was power-cycled. The peer reset in the middle of the
// connect handshake and furble crashed. After the reboot, connecting worked
// normally.
//
// Root cause: Camera::connect() armed setSelfDelete(true, true) on the NimBLE
// client for the whole attempt. The vendor _connect() runs on the control task
// and dereferences the client repeatedly (service discovery, pairing writes,
// subscriptions). When the peer resets mid-handshake, onDisconnect fires on the
// NimBLE host task and a self-deleting client is FREED there, while _connect()
// is still running and about to dereference that same client. The next
// m_Client->getService()/getCharacteristic() then lands on freed memory.
//
// The fix keeps self-delete OFF for the connect window (setSelfDelete(false,
// false)) so the client cannot be freed out from under _connect(). A peer drop
// only clears the connected flag; _connect() unwinds cleanly, and Camera::connect()
// reclaims the client deterministically on the control task, detaching the
// callbacks first. On success self-delete is restored for the live session.
//
// This binary compiles lib/furble/Camera.cpp with -fsanitize=address so the
// mid-connect dereference is instrumented. The peer's dropLinkDuringConnect
// fault severs the link during the identify write and, because the client is
// self-deleting on the unfixed code, frees it inline; _connect() then touches
// the freed client and ASan aborts. With the fix the client survives, so the
// connect fails cleanly and the test passes. Restoring setSelfDelete(true, true)
// in Camera::connect() reproduces the ASan abort, so the test is mutation proven.

#include <cstdint>
#include <iostream>
#include <memory>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

std::shared_ptr<Furble::FujifilmBasic> makeCamera(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

// A peer that resets in the middle of the connect handshake must not crash
// furble: the half-finished _connect() has to unwind cleanly, leave the camera
// disconnected, and reclaim the client instead of dereferencing a freed one.
bool testMidConnectDropDoesNotCrash() {
  std::cout << "test: a peer reset mid-connect unwinds cleanly with no use-after-free\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  // Sever the link during the identify write, part way through the handshake and
  // after several client dereferences have already succeeded. On the unfixed
  // code the self-deleting client is freed here and the subsequent subscribe
  // dereferences it; the fix keeps the client alive so _connect() fails cleanly.
  peer.dropLinkDuringConnect(Furble::Host::FujifilmVirtualCamera::pairServiceUUID(),
                             Furble::Host::FujifilmVirtualCamera::identifierCharacteristicUUID());
  auto camera = makeCamera(peer);

  const bool connected = camera->connect(ESP_PWR_LVL_P3, 1000);

  check(!connected, "the mid-connect drop makes connect() report failure");
  check(!camera->isConnected(), "the camera is left disconnected, not a zombie");
  check(NimBLEDevice::liveClientCount() == 0,
        "the aborted connect reclaims the client, none leak from the pool");

  // The camera must recover: with the peer healthy again a fresh connect runs the
  // real handshake and reports connected, mirroring the on-device behaviour where
  // connecting worked normally afterwards.
  peer.clearFaults();
  peer.clearEvents();
  const bool reconnected = camera->connect(ESP_PWR_LVL_P3, 1000);
  check(reconnected, "a fresh connect after the mid-connect drop succeeds");
  check(camera->isConnected(), "the recovered connect reports connected");
  check(peer.connected() && peer.tokenAccepted(),
        "the recovered connect performs the real handshake");

  camera->disconnect();
  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testMidConnectDropDoesNotCrash();

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "connect mid-drop harness: PASS\n";
  } else {
    std::cout << "connect mid-drop harness: FAIL (" << g_Failures << " checks)\n";
  }
  return status;
}
