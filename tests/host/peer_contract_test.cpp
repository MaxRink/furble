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

  if (g_Failures != 0) {
    std::fprintf(stderr, "%d peer contract checks failed\n", g_Failures);
    return 1;
  }
  std::printf("all peer contracts passed\n");
  return 0;
}
