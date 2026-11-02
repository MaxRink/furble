// Contract test for every virtual camera peer.
//
// It proves that each peer constructs, advertises, exposes its primary service,
// exposes a writable characteristic, and carries a non-empty manufacturer /
// service payload. It does not prove camera behavior or a physical outcome.

#include <cstdio>
#include <string>

#include "CanonRemoteVirtualCamera.h"
#include "CanonSmartVirtualCamera.h"
#include "DjiVirtualCamera.h"
#include "LumixVirtualCamera.h"
#include "NikonRemoteVirtualCamera.h"
#include "NikonSmartVirtualCamera.h"
#include "SonyVirtualCamera.h"

namespace {

int g_Failures = 0;

void check(bool condition, const char *peer, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s: %s\n", peer, message);
    g_Failures++;
  }
}

template <typename Peer>
void checkPeer(const char *name,
               const NimBLEUUID &service,
               const NimBLEUUID &characteristic,
               bool writable) {
  Peer peer;
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  check(advertisement.getName() == peer.config().name, name, "advertisement name");
  check(advertisement.getAddress() == peer.config().address, name, "advertisement address");
  const bool hasManufacturer = advertisement.getManufacturerData().length() != 0;
  check(hasManufacturer || advertisement.haveServiceUUID(), name, "advertisement identity");
  check(peer.hasService(service), name, "primary service");
  check(peer.hasCharacteristic(service, characteristic), name, "known characteristic");
  if (writable) {
    check(peer.canWrite(service, characteristic), name, "writable characteristic");
  }
}

void checkNikonReconnectIdentity() {
  using namespace Furble::Host;
  {
    NikonRemoteVirtualCamera::Config config;
    NikonRemoteVirtualCamera peer(config);
    NimBLEClient client;
    check(peer.acceptConnection(client, config.address), "NikonRemote", "accepts a link");
    std::vector<uint8_t> stage1(17, 0x00);
    stage1[0] = 0x01;
    stage1[9] = 0xa1;
    stage1[10] = 0xb2;
    stage1[11] = 0xc3;
    stage1[12] = 0xd4;
    check(peer.write(client, NikonRemoteVirtualCamera::serviceUUID(),
                     NikonRemoteVirtualCamera::pairingCharacteristicUUID(), stage1, true),
          "NikonRemote", "accepts pairing stage 1");
    peer.setPaired(true);
    const auto advertisement = peer.advertisement();
    const auto data = advertisement.getManufacturerData();
    check(data.size() == 7 && data[0] == 0x99 && data[1] == 0x03 && data[2] == 0xa1
              && data[3] == 0xb2 && data[4] == 0xc3 && data[5] == 0xd4,
          "NikonRemote", "reconnect advertisement echoes the stage-1 device ID");
  }
  {
    NikonSmartVirtualCamera::Config config;
    NikonSmartVirtualCamera peer(config);
    NimBLEClient client;
    check(peer.acceptConnection(client, config.address), "NikonSmart", "accepts a link");
    std::vector<uint8_t> stage1(17, 0x00);
    stage1[0] = 0x01;
    stage1[9] = 0x11;
    stage1[10] = 0x22;
    stage1[11] = 0x33;
    stage1[12] = 0x44;
    check(peer.write(client, NikonSmartVirtualCamera::serviceUUID(),
                     NikonSmartVirtualCamera::authCharacteristicUUID(), stage1, true),
          "NikonSmart", "accepts auth stage 1");
    check(!peer.authenticated(), "NikonSmart", "stage 1 alone does not authenticate");
    peer.setPaired(true);
    const auto advertisement = peer.advertisement();
    const auto data = advertisement.getManufacturerData();
    check(data.size() == 7 && data[0] == 0x99 && data[1] == 0x03 && data[2] == 0x11
              && data[3] == 0x22 && data[4] == 0x33 && data[5] == 0x44,
          "NikonSmart", "reconnect advertisement echoes the stage-1 device ID");
  }
}

void checkLumixClockLayout() {
  using namespace Furble::Host;
  LumixVirtualCamera::Config config;
  // Keep the clock path independent of the registration handshake; this test
  // only pins the layout disambiguation.
  config.require_registration_for_control = false;
  LumixVirtualCamera peer(config);
  NimBLEClient client;
  check(peer.acceptConnection(client, config.address), "Lumix", "accepts a link");
  // A valid App packet with UTC offset zero and DST enabled. Read as G9II it
  // would claim a 256 minute offset, so the layout must stay App.
  const std::vector<uint8_t> clock = {0, 0, 1, 1, 12, 0, 0, 0x00, 0x00, 0x01};
  check(peer.write(client, LumixVirtualCamera::clockServiceUUID(),
                   LumixVirtualCamera::clockCharacteristicUUID(), clock, true),
        "Lumix", "accepts a UTC App clock packet");
  check(peer.lastClockLayout() == LumixVirtualCamera::ClockLayout::App, "Lumix",
        "UTC App packet is not misread as G9II");
  check(peer.clockOffsetMinutes() == 0 && peer.clockDst() == 1, "Lumix",
        "App offset and DST are preserved");
}

}  // namespace

int main() {
  using namespace Furble::Host;

  checkPeer<CanonSmartVirtualCamera>("CanonSmart", CanonSmartVirtualCamera::primaryServiceUUID(),
                                     CanonSmartVirtualCamera::pairingCharacteristicUUID(), true);
  checkPeer<CanonRemoteVirtualCamera>("CanonRemote", CanonRemoteVirtualCamera::remoteServiceUUID(),
                                      CanonRemoteVirtualCamera::controlCharacteristicUUID(), true);
  checkPeer<NikonRemoteVirtualCamera>("NikonRemote", NikonRemoteVirtualCamera::serviceUUID(),
                                      NikonRemoteVirtualCamera::shutterCharacteristicUUID(), true);
  checkPeer<NikonSmartVirtualCamera>("NikonSmart", NikonSmartVirtualCamera::serviceUUID(),
                                     NikonSmartVirtualCamera::authCharacteristicUUID(), true);
  checkPeer<SonyVirtualCamera>("Sony", SonyVirtualCamera::cameraServiceUUID(),
                               SonyVirtualCamera::cc02CharacteristicUUID(), true);
  checkPeer<LumixVirtualCamera>("Lumix", LumixVirtualCamera::sessionServiceUUID(),
                                LumixVirtualCamera::mei0CharacteristicUUID(), true);
  checkPeer<DjiVirtualCamera>("Dji", DjiVirtualCamera::serviceUUID(),
                              DjiVirtualCamera::writeCharacteristicUUID(), true);
  checkNikonReconnectIdentity();
  checkLumixClockLayout();

  if (g_Failures != 0) {
    std::fprintf(stderr, "%d peer contract checks failed\n", g_Failures);
    return 1;
  }
  std::printf("all peer contracts passed\n");
  return 0;
}
