// Sony location delivery across the advertisement generations.
//
// Upstream issue 316: a 2019 ILCE-7RM4 was invisible to discovery, refused
// pairing and never received location data. Discovery is covered by
// sony_advertisement_test; this suite drives the production Sony location path
// against the virtual peer for
//
//   1. the current generation, whose DD30/DD31 handshake is unchanged,
//   2. the legacy generation, which exposes neither DD30 nor DD31 and gets the
//      direct DD11 write,
//   3. an ATT MTU too small to carry the 91 or 95 byte packet, where the write
//      must not be attempted at all instead of failing every GPS tick,
//   4. a body that refuses DD11 anyway, where the failure is a logged no-op and
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

/** Every DD11 location update the peer accepted or rejected. */
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
  bool expectConnected;
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

  const bool connected = camera.connect(Furble::TxPower::P3, 1000);
  passed &= check(connected == item.expectConnected, item.label);
  if (!connected) {
    return passed;
  }

  passed &= check(camera.geoGate() == item.expectGate,
                  "the DD00 profile selects the expected location gate");

  // The exchange has to be requested with at least the packet the location path
  // needs, and the result has to be visible on the link.
  passed &= check(peer.requestedAttMtu() >= Furble::Camera::ATT_MTU_FLOOR,
                  "the connect path requests at least the ATT MTU floor");
  // ATT settles on the smaller of the two preferences. Read the local one back
  // after connect: the production path raises it to the floor before the link.
  const uint16_t localPreferred = NimBLEDevice::getMTU();
  passed &= check(localPreferred >= Furble::Camera::ATT_MTU_FLOOR,
                  "the preferred ATT MTU was raised to at least the floor");
  const uint16_t settled = std::min<uint16_t>(localPreferred, item.config.max_att_mtu);
  passed &= check(peer.negotiatedAttMtu() == settled,
                  "the link settles on the smaller of the two MTU preferences");
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

  camera.updateGeoData(gps(), timesync());
  const size_t afterSecond = locationWrites(peer);
  if (item.expectNoRetry) {
    passed &= check(afterSecond == afterFirst, "an impossible location write is not retried");
  }

  camera.disconnect();
  return passed;
}

}  // namespace

int main() {
  using Furble::Host::SonyVirtualCamera;

  Furble::Device::init(Furble::TxPower::P3);
  bool passed = true;

  // 1. Current generation: DD30/DD31 present, the existing handshake runs, the
  //    95-byte form is delivered as before.
  Case current;
  current.label = "the current generation connects and delivers a location";
  current.config = SonyVirtualCamera::Config {};
  current.local_preferred_mtu = 0;
  current.expectConnected = true;
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
  legacy.expectConnected = true;
  legacy.expectGeneration = Furble::Sony::Generation::Legacy;
  legacy.expectGate = Furble::Sony::GeoGate::BestEffort;
  legacy.expectLocationWrite = true;
  legacy.expectNoRetry = false;
  passed &= run(legacy);

  {
    // The best-effort path must not write DD30 or DD31, which do not exist.
    NimBLEDevice::resetMock();
    SonyVirtualCamera peer(SonyVirtualCamera::legacyBody());
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the legacy body connects")) {
      camera.updateGeoData(gps(), timesync());
      passed &= check(!hasCommand(peer, "DD30 location lock"),
                      "the legacy body is never asked for a location lock");
      passed &= check(!hasCommand(peer, "DD31 location enable"),
                      "the legacy body is never asked to enable the transfer");
      passed &= check(peer.lastLocation().size() == 95,
                      "the legacy body receives the 95-byte location form");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 3. A link that cannot carry the packet: the peer answers the exchange with
  //    the 23-byte ATT minimum. The write must not be attempted and the camera
  //    must stay connected; a location is simply absent for the session.
  Case narrowLegacy;
  narrowLegacy.label = "a 23-byte MTU still connects";
  narrowLegacy.config = SonyVirtualCamera::legacyBody();
  narrowLegacy.config.max_att_mtu = 23;
  narrowLegacy.local_preferred_mtu = 0;
  narrowLegacy.expectConnected = true;
  narrowLegacy.expectGeneration = Furble::Sony::Generation::Legacy;
  narrowLegacy.expectGate = Furble::Sony::GeoGate::BestEffort;
  narrowLegacy.expectLocationWrite = false;
  narrowLegacy.expectNoRetry = true;

  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera peer(narrowLegacy.config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    const bool connected = camera.connect(Furble::TxPower::P3, 1000);
    passed &= check(connected, "a body that answers the MTU exchange with 23 connects");
    if (connected) {
      passed &= check(peer.negotiatedAttMtu() == 23, "the link stays at the ATT minimum");
      passed &= check(camera.isConnected(), "the camera reports the live link");
      camera.updateGeoData(gps(), timesync());
      passed &= check(locationWrites(peer) == 0,
                      "an MTU below the packet size produces no write attempt");
      camera.updateGeoData(gps(), timesync());
      passed &= check(locationWrites(peer) == 0, "the skipped write is not retried");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 3b. A host whose preferred MTU is the 23-byte ATT minimum: the connect path
  //     has to raise the request to the floor before the link, or the location
  //     packet never fits and the write fails at the ATT layer.
  Case lowPreferred;
  lowPreferred.label = "a low preferred MTU is raised before the link";
  lowPreferred.config = SonyVirtualCamera::legacyBody();
  lowPreferred.local_preferred_mtu = 23;
  lowPreferred.expectConnected = true;
  lowPreferred.expectGeneration = Furble::Sony::Generation::Legacy;
  lowPreferred.expectGate = Furble::Sony::GeoGate::BestEffort;
  lowPreferred.expectLocationWrite = true;
  lowPreferred.expectNoRetry = false;

  {
    NimBLEDevice::resetMock();
    NimBLEDevice::setMTU(23);
    SonyVirtualCamera peer(lowPreferred.config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    const bool connected = camera.connect(Furble::TxPower::P3, 1000);
    passed &= check(connected, "a host at the ATT minimum still connects");
    if (connected) {
      passed &= check(peer.requestedAttMtu() >= Furble::Camera::ATT_MTU_FLOOR,
                      "the exchange requests at least the ATT MTU floor");
      passed &=
          check(peer.negotiatedAttMtu() >= 98, "the negotiated link can carry the 95-byte packet");
      camera.updateGeoData(gps(), timesync());
      passed &= check(locationWrites(peer) == 1, "the location write fits and is accepted");
      passed &= check(NimBLEDevice::mockAttWriteRejections() == 0,
                      "the location write was never rejected for size");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 4. Best effort does not retry a body that refuses DD11: the first refusal
  //    ends the attempts for the session, without surfacing an error state.
  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera peer(SonyVirtualCamera::legacyBody());
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the refusing body connects")) {
      peer.setNextFault(SonyVirtualCamera::Fault::Reject);
      camera.updateGeoData(gps(), timesync());
      const size_t refused = locationWrites(peer);
      passed &= check(refused == 1, "the refused write was attempted once");
      passed &= check(camera.isConnected(), "a refused location write keeps the session");
      camera.updateGeoData(gps(), timesync());
      passed &=
          check(locationWrites(peer) == refused, "a refused best-effort write is not retried");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 5. A current-generation body whose discovery comes back without the pair
  //    keeps the strict gate, so today's behaviour does not change for the
  //    bodies that already work, and no unproven direct write happens.
  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera::Config strictOnly;
    strictOnly.location_lock_characteristics_present = false;
    SonyVirtualCamera peer(strictOnly);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the current body connects")) {
      passed &= check(camera.geoGate() == Furble::Sony::GeoGate::Strict,
                      "the current generation keeps the strict gate");
      camera.updateGeoData(gps(), timesync());
      passed &=
          check(locationWrites(peer) == 0, "the strict gate delivers no location without the pair");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  // 6. A legacy body that does expose the pair keeps the strict handshake: the
  //    discovered profile is the authority, not the advertised generation.
  {
    NimBLEDevice::resetMock();
    SonyVirtualCamera::Config legacyWithPair = SonyVirtualCamera::legacyBody();
    legacyWithPair.location_lock_characteristics_present = true;
    SonyVirtualCamera peer(legacyWithPair);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Sony camera(&advertisement);
    if (check(camera.connect(Furble::TxPower::P3, 1000), "the legacy body with a pair connects")) {
      passed &= check(camera.geoGate() == Furble::Sony::GeoGate::Strict,
                      "an exposed DD30/DD31 pair selects the strict gate");
      camera.updateGeoData(gps(), timesync());
      passed &= check(hasCommand(peer, "DD30 location lock"),
                      "the strict path takes the lock before the location write");
      passed &= check(locationWrites(peer) == 1, "the strict path delivers the location");
      camera.disconnect();
    } else {
      passed = false;
    }
  }

  NimBLEDevice::resetMock();
  return passed ? 0 : 1;
}
