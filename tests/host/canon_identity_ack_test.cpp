// Host regression for the Canon identity writes running without an ATT
// acknowledgement.
//
// Both Canon roles identify themselves with a prefixed write in the connect
// sequence: Canon Smart sends the client name, the UUID and the two accept
// bytes to 0001000a, and Canon Remote sends the client name to 00050002. A
// write without response returns as soon as the PDU is handed to the
// controller, so the connect sequence could not tell an accepted identity step
// from a refused one, and a camera that dropped the frame was still promoted as
// paired (plans/96 A4, plans/97-followups.md Group 1 batch 4).
//
// The peers record the `response` flag of every write they receive, so this
// asserts the request shape the camera actually sees. Red-first evidence: with
// `writePrefix()` on write-without-response every identity write below reports
// `response == false`.
//
// Vendor status: Canon is not testable on this bench. This is host coverage
// against the modelled peers only, and it does not claim a device result.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "CanonEOSRemote.h"
#include "CanonEOSSmart.h"
#include "CanonRemoteVirtualCamera.h"
#include "CanonSmartVirtualCamera.h"
#include "Device.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-canon-identity";

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

/** Every write the peer received on one characteristic. */
std::vector<Furble::Host::CanonSmartVirtualCamera::Write> writesTo(
    const std::vector<Furble::Host::CanonSmartVirtualCamera::Write> &writes,
    const std::string &characteristic) {
  std::vector<Furble::Host::CanonSmartVirtualCamera::Write> matched;
  for (const auto &write : writes) {
    if (write.characteristic == characteristic) {
      matched.push_back(write);
    }
  }
  return matched;
}

// Canon Smart: the saved-reconnect path, so the 60 s pairing gate is satisfied
// by the bond and the identity writes are the whole of the interesting work.
void testSmartIdentityWritesAskForAnAck() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  Furble::Host::CanonSmartVirtualCamera::Config config;
  config.camera_bonded = true;
  Furble::Host::CanonSmartVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setBonded(true);

  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Furble::CanonEOSSmart camera(&advertisement);

  check(camera.connect(Furble::TxPower::P3, 5000), "the bonded Canon Smart reconnect completes");
  check(camera.isConnected(), "the Canon Smart link is up");

  const auto identity =
      writesTo(peer.writes(),
               Furble::Host::CanonSmartVirtualCamera::identityCharacteristicUUID().toString());
  check(!identity.empty(), "the connect sequence reached the identity characteristic");

  bool sawAccept = false;
  bool sawName = false;
  for (const auto &write : identity) {
    check(write.response, "every Canon Smart identity write asks for the ATT response");
    if (write.payload.size() == 1 && write.payload[0] == 0x01) {
      sawAccept = true;
    }
    if (write.payload.size() > 1 && write.payload[0] == 0x04) {
      sawName = true;
    }
  }
  check(sawAccept, "the identity accept byte was sent");
  check(sawName, "the client name frame was sent");

  // The name characteristic carries the first identity frame (prefix 0x01).
  const auto name = writesTo(
      peer.writes(), Furble::Host::CanonSmartVirtualCamera::pairingCharacteristicUUID().toString());
  check(!name.empty(), "the connect sequence reached the name characteristic");
  for (const auto &write : name) {
    check(write.response, "the Canon Smart client-name write asks for the ATT response");
  }

  camera.disconnect();
}

// Canon Remote: one identifying write, same helper, same requirement.
void testRemoteIdentityWriteAsksForAnAck() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  Furble::Host::CanonRemoteVirtualCamera::Config config;
  config.camera_bonded = true;
  Furble::Host::CanonRemoteVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setBonded(true);

  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  Furble::CanonEOSRemote camera(&advertisement);

  check(camera.connect(Furble::TxPower::P3, 5000), "the bonded Canon Remote reconnect completes");

  bool sawIdentify = false;
  for (const auto &write : peer.writes()) {
    if (write.characteristic
        != Furble::Host::CanonRemoteVirtualCamera::nameCharacteristicUUID().toString()) {
      continue;
    }
    sawIdentify = true;
    check(write.response, "the Canon Remote identify write asks for the ATT response");
  }
  check(sawIdentify, "the Canon Remote identify write was sent");

  camera.disconnect();
}

}  // namespace

int main() {
  testSmartIdentityWritesAskForAnAck();
  testRemoteIdentityWriteAsksForAnAck();

  if (g_Failures != 0) {
    std::cerr << g_Failures << " Canon identity checks failed\n";
    return 1;
  }

  std::cout << "Canon identity writes request an acknowledgement\n";
  return 0;
}
