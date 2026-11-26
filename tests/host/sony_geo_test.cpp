// Sony location delivery across the advertisement generations.
//
// Upstream issue 316: a 2019 ILCE-7RM4 was invisible to discovery, refused
// pairing and never received location data. Discovery itself is covered by
// sony_advertisement_test; this suite drives the production Sony location path
// against the virtual peer for
//
//   1. the current generation, whose DD30/DD31 handshake is unchanged,
//   2. the legacy generation, which exposes neither DD30 nor DD31 and gets the
//      direct DD11 write,
//   3. a peer that answers the MTU exchange with the 23-byte ATT minimum, where
//      the write must not be attempted instead of failing on every GPS tick,
//   4. a host whose preferred MTU is the 23-byte minimum, where the connect path
//      has to raise the request to the floor before the link,
//   5. a body that refuses DD11 anyway, where the failure is a logged no-op and
//      is not retried for the rest of the session.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Sony.h"
#include "SonyVirtualCamera.h"

const char *LOG_TAG = "sony-geo-test";

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

Furble::Camera::gps_t gps() {
  return {52.5, 13.4, 34.0, 8};
}

Furble::Camera::timesync_t timesync() {
  return {
      2026, 11, 1, 12, 34, 56, 0, {2026, 11, 1, 13, 34, 56, 0},
             120, 60, 60, true
  };
}

/** Every DD11 location update the peer saw, accepted or rejected. */
size_t locationWrites(const Furble::Host::SonyVirtualCamera &peer) {
  return static_cast<size_t>(std::count_if(
      peer.writes().begin(), peer.writes().end(),
      [](const auto &write) { return write.command.rfind("DD11 location update", 0) == 0; }));
}

bool hasCommand(const Furble::Host::SonyVirtualCamera &peer, const std::string &prefix) {
  return std::any_of(peer.writes().begin(), peer.writes().end(),
                     [&prefix](const auto &write) { return write.command.rfind(prefix, 0) == 0; });
}

struct Case {
  const char *label;
  Furble::Host::SonyVirtualCamera::Config config;
  /** Local preferred ATT MTU to force before connecting, 0 keeps the default. */
  uint16_t local_preferred_mtu;
  Furble::Sony::Generation expectGeneration;
  Furble::Sony::GeoGate expectGate;
  bool expectLocationWrite;
  /** Second GPS tick must not retry a refused or impossible write. */
  bool expectNoRetry;
};

bool run(const Case &item) {
  using Furble::Host::SonyVirtualCamera;

  NimBLEDevice::resetMock();
  if (item.local_preferred_mtu != 0) {
    NimBLEDevice::setMTU(item.local_preferred_mtu);
  }
  SonyVirtualCamera peer(item.config);
  NimBLEDevice::setMockPeer(&peer);

  const auto advertisement = peer.advertisement();
  bool passed =
      check(Furble::Sony::matches(&advertisement), "the advertisement reaches the Sony matcher");

  Furble::Sony camera(&advertisement);
  passed &=
      check(camera.generation() == item.expectGeneration, "the advertised generation is recorded");

  if (!check(camera.connect(Furble::TxPower::P3, 1000), item.label)) {
    return false;
  }
  passed &= check(camera.isConnected(), "the camera reports the live link");
  passed &= check(camera.geoGate() == item.expectGate,
                  "the DD00 profile selects the expected location gate");

  // The exchange has to be requested with at least the packet the location path
  // needs. Read the local preference back after connect: the production path
  // raises it to the floor before the link, and ATT then settles on the smaller
  // of the two preferences.
  passed &= check(peer.requestedAttMtu() >= Furble::Camera::ATT_MTU_FLOOR,
                  "the connect path requests at least the ATT MTU floor");
  const uint16_t localPreferred = NimBLEDevice::getMTU();
  passed &= check(localPreferred >= Furble::Camera::ATT_MTU_FLOOR,
                  "the preferred ATT MTU is at least the floor");
  const uint16_t settled = std::min<uint16_t>(localPreferred, item.config.max_att_mtu);
  passed &= check(peer.negotiatedAttMtu() == settled,
                  "the link settles on the smaller of the two MTU preferences");

  // A packeted body must never be handed a location the link cannot frame: the
  // host would fail it with BLE_HS_EMSGSIZE before the peer saw it.
  passed &= check(NimBLEDevice::mockAttWriteRejections() == 0,
                  "no write is handed to a link that cannot carry it");

  const size_t before = locationWrites(peer);
  camera.updateGeoData(gps(), timesync());
  const size_t afterFirst = locationWrites(peer);
  if (item.expectLocationWrite) {
    passed &= check(afterFirst > before, "the location update reaches the peer");
  } else {
    passed &= check(afterFirst == before, "no location write is attempted");
  }

  // The allow/enable handshake runs only on a body that exposes both halves of
  // the pair. A profile with one half keeps the strict gate and sends nothing.
  if (item.config.location_allow_present && item.config.location_enable_present) {
    passed &=
        check(hasCommand(peer, "DD30 location lock"), "the strict path asks for the location lock");
    passed &= check(hasCommand(peer, "DD31 location enable"),
                    "the strict path enables the location transfer");
  } else {
    passed &= check(!hasCommand(peer, "DD30 location lock"),
                    "a body without DD30 is never asked for a location lock");
    passed &= check(!hasCommand(peer, "DD31 location enable"),
                    "a body without DD31 is never asked to enable the transfer");
  }
  if (item.expectLocationWrite) {
    const size_t expectedForm = item.config.require_91_byte_location ? 91 : 95;
    passed &= check(peer.lastLocation().size() == expectedForm,
                    "the delivered location uses the expected form");
  }

  camera.updateGeoData(gps(), timesync());
  if (item.expectNoRetry) {
    passed &= check(locationWrites(peer) == afterFirst, "an impossible write is not retried");
  }

  camera.disconnect();
  return passed;
}

}  // namespace

int main() {
  using Furble::Host::SonyVirtualCamera;

  Furble::Device::init(Furble::TxPower::P3);
  bool passed = true;

  // 1. Current generation: DD30/DD31 present, the existing handshake runs and
  //    the 95-byte form is delivered as before.
  Case current;
  current.label = "the current generation connects and delivers a location";
  current.config = SonyVirtualCamera::Config {};
  current.local_preferred_mtu = 0;
  current.expectGeneration = Furble::Sony::Generation::Current;
  current.expectGate = Furble::Sony::GeoGate::Strict;
  current.expectLocationWrite = true;
  current.expectNoRetry = false;
  passed &= run(current);

  // 2. Legacy generation, the 7RM4: tag 0x22 bit 1 clear and no DD30/DD31.
  //    Discovery must accept it, the profile selects the best-effort gate, and
  //    DD11 is written without any allow or enable step.
  Case legacy;
  legacy.label = "the legacy generation connects and delivers a location";
  legacy.config = SonyVirtualCamera::legacyBody();
  legacy.local_preferred_mtu = 0;
  legacy.expectGeneration = Furble::Sony::Generation::Legacy;
  legacy.expectGate = Furble::Sony::GeoGate::BestEffort;
  legacy.expectLocationWrite = true;
  legacy.expectNoRetry = false;
  passed &= run(legacy);

  // 3. A peer that answers the exchange with the 23-byte ATT minimum. The write
  //    must not be attempted and the camera must stay connected: a location is
  //    simply absent for that session.
  Case narrowPeer;
  narrowPeer.label = "a body that answers the MTU exchange with 23 still connects";
  narrowPeer.config = SonyVirtualCamera::legacyBody();
  narrowPeer.config.max_att_mtu = 23;
  narrowPeer.local_preferred_mtu = 0;
  narrowPeer.expectGeneration = Furble::Sony::Generation::Legacy;
  narrowPeer.expectGate = Furble::Sony::GeoGate::BestEffort;
  narrowPeer.expectLocationWrite = false;
  narrowPeer.expectNoRetry = true;
  passed &= run(narrowPeer);

  // 3b. Half a gate is not an absent one: a body that exposes only DD30 keeps
  //     the strict gate, so a short discovery never becomes an ungated write.
  Case partialPair;
  partialPair.label = "a body with only DD30 keeps the strict gate";
  partialPair.config = SonyVirtualCamera::legacyBody();
  partialPair.config.location_allow_present = true;
  partialPair.local_preferred_mtu = 0;
  partialPair.expectGeneration = Furble::Sony::Generation::Legacy;
  partialPair.expectGate = Furble::Sony::GeoGate::Strict;
  partialPair.expectLocationWrite = false;
  partialPair.expectNoRetry = true;
  passed &= run(partialPair);

  // 4. A host whose preferred MTU is the ATT minimum: the connect path raises
  //    the request to the floor before the link, so the packet still fits.
  Case lowPreferred;
  lowPreferred.label = "a low preferred MTU is raised before the link";
  lowPreferred.config = SonyVirtualCamera::legacyBody();
  lowPreferred.local_preferred_mtu = 23;
  lowPreferred.expectGeneration = Furble::Sony::Generation::Legacy;
  lowPreferred.expectGate = Furble::Sony::GeoGate::BestEffort;
  lowPreferred.expectLocationWrite = true;
  lowPreferred.expectNoRetry = false;
  passed &= run(lowPreferred);

  // 5. A current-generation body whose discovery comes back without the pair
  //    keeps the strict gate, so the bodies that work today do not change and
  //    no unproven direct write happens.
  Case currentWithoutPair;
  currentWithoutPair.label = "a current body without the pair keeps the strict gate";
  currentWithoutPair.config = SonyVirtualCamera::Config {};
  currentWithoutPair.config.location_allow_present = false;
  currentWithoutPair.config.location_enable_present = false;
  currentWithoutPair.local_preferred_mtu = 0;
  currentWithoutPair.expectGeneration = Furble::Sony::Generation::Current;
  currentWithoutPair.expectGate = Furble::Sony::GeoGate::Strict;
  currentWithoutPair.expectLocationWrite = false;
  currentWithoutPair.expectNoRetry = true;
  passed &= run(currentWithoutPair);

  // 6. A legacy body that does expose the pair keeps the strict handshake: the
  //    discovered profile is the authority, not the advertised generation.
  Case legacyWithPair;
  legacyWithPair.label = "a legacy body with the pair keeps the strict gate";
  legacyWithPair.config = SonyVirtualCamera::legacyBody();
  legacyWithPair.config.location_allow_present = true;
  legacyWithPair.config.location_enable_present = true;
  legacyWithPair.local_preferred_mtu = 0;
  legacyWithPair.expectGeneration = Furble::Sony::Generation::Legacy;
  legacyWithPair.expectGate = Furble::Sony::GeoGate::Strict;
  legacyWithPair.expectLocationWrite = true;
  legacyWithPair.expectNoRetry = false;
  passed &= run(legacyWithPair);

  // 7. A retryable refusal is not a capability gap. One busy answer must not
  //    end the session's attempts: the next tick writes again and succeeds.
  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera peer(SonyVirtualCamera::legacyBody());
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the busy body connects")) {
      peer.setNextFault(SonyVirtualCamera::Fault::Busy);
      camera.updateGeoData(gps(), timesync());
      const size_t refused = locationWrites(peer);
      passed &= check(refused == 1, "the busy write was attempted once");
      passed &= check(camera.isConnected(), "a busy location write keeps the session");
      camera.updateGeoData(gps(), timesync());
      passed &=
          check(locationWrites(peer) == refused + 1, "a busy write is retried on the next tick");
      passed &= check(peer.lastLocation().size() == 95, "the retried write delivers the location");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 8. A body that refuses every write stops after GEO_FAILURE_LIMIT attempts,
  //    so a capability refusal costs a bounded number of writes rather than one
  //    per GPS tick.
  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera peer(SonyVirtualCamera::legacyBody());
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the refusing body connects")) {
      for (uint8_t attempt = 0; attempt < Furble::Sony::GEO_FAILURE_LIMIT; attempt++) {
        peer.setNextFault(SonyVirtualCamera::Fault::Reject);
        camera.updateGeoData(gps(), timesync());
      }
      const size_t refused = locationWrites(peer);
      passed &= check(refused == Furble::Sony::GEO_FAILURE_LIMIT,
                      "the refusing body was tried the bounded number of times");
      passed &= check(camera.isConnected(), "a refused location write keeps the session");
      camera.updateGeoData(gps(), timesync());
      passed &= check(locationWrites(peer) == refused,
                      "a body that refuses every write is not retried past the bound");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  NimBLEDevice::resetMock();
  return passed ? 0 : 1;
}
