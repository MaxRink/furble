// LUMIX session handshake regression.
//
// The official app sends Confirm 01, waits for the control readiness
// notification, then Start 02. A bare ShutterOn 04 is refused by a body that
// requires the session gate. 01 and 02 are never focus commands.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "Lumix.h"
#include "LumixVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "lumix-session-test";

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool hasClockWrite(const std::vector<Furble::Host::LumixVirtualCamera::Write> &writes) {
  for (const auto &write : writes) {
    if (write.payload.size() == 10) {
      return true;
    }
  }
  return false;
}

bool hasGpsWrite(const std::vector<Furble::Host::LumixVirtualCamera::Write> &writes) {
  for (const auto &write : writes) {
    if (write.payload.size() == 16) {
      return true;
    }
  }
  return false;
}

bool hasCommand(const std::vector<Furble::Host::LumixVirtualCamera::Write> &writes,
                uint8_t command) {
  for (const auto &write : writes) {
    if (write.payload.size() == 1 && write.payload[0] == command) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  Furble::Host::LumixVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Lumix camera(&advertisement);

  bool passed = check(camera.connect(Furble::TxPower::P3, 2000),
                      "LUMIX connect completes the Confirm and Start handshake");
  passed &= check(peer.confirmed(), "LUMIX Confirm 01 reaches the camera");
  passed &= check(peer.sessionStarted(), "LUMIX Start 02 starts the session");

  const auto &connectWrites = peer.writes();
  passed &= check(hasCommand(connectWrites, 0x01), "LUMIX connect writes Confirm 01");
  passed &= check(hasCommand(connectWrites, 0x02), "LUMIX connect writes Start 02");

  peer.clearEvents();
  camera.focusPress();
  camera.focusRelease();
  passed &=
      check(peer.writes().empty(), "LUMIX focus press and release emit no session or focus write");

  peer.clearEvents();
  camera.shutterPress();
  passed &=
      check(peer.writes().size() == 1 && peer.writes()[0].payload == std::vector<uint8_t> {0x04},
            "LUMIX shutter press writes ShutterOn 04");
  passed &= check(peer.shutterHeld(), "LUMIX shutter press is accepted inside the session");

  camera.shutterRelease();
  passed &=
      check(peer.writes().size() == 2 && peer.writes()[1].payload == std::vector<uint8_t> {0x05},
            "LUMIX shutter release writes ShutterOff 05");

  const Furble::Camera::gps_t gps = {52.5, 13.4, 34.0, 8};
  const Furble::Camera::timesync_t time = {
      2026, 11, 1, 12, 34, 56, 0, {2026, 11, 1, 12, 34, 56, 0},
             0, 0, 0, false
  };
  camera.updateGeoData(gps, time);
  passed &= check(peer.lastGps().size() == 16, "LUMIX GPS packet is written");
  passed &= check(peer.clockOffsetMinutes() == 0 && peer.clockDst() == 0,
                  "the UTC clock carries a zero offset and no DST");

  Furble::Camera::timesync_t zoned = time;
  zoned.local = {2026, 11, 1, 13, 34, 56, 0};
  zoned.tz_offset_minutes = 120;
  zoned.tz_standard_offset_minutes = 60;
  zoned.tz_dst_offset_minutes = 60;
  zoned.dst = true;
  camera.updateGeoData(gps, zoned);
  passed &= check(peer.clockOffsetMinutes() == 60 && peer.clockDst() == 1,
                  "the LUMIX clock carries local civil time and the standard offset plus DST");

  peer.clearEvents();
  camera.disconnect();
  passed &= check(peer.gpsStopped(), "LUMIX disconnect sends the GPS stop packet");
  passed &= check(hasCommand(peer.writes(), 0x03), "LUMIX disconnect writes Finish 03");
  passed &= check(peer.sessionFinished(), "LUMIX Finish 03 closes the session");

  NimBLEDevice::resetMock();

  // Readiness gate. A body that requires location readiness must not receive a
  // GPS packet before 37701b80 reports 01, and must not receive a clock packet
  // before 4cf487c0 reports 01.
  Furble::Host::LumixVirtualCamera::Config gated;
  gated.readiness_on_subscribe = false;
  gated.gps_requires_readiness = true;
  gated.clock_requires_readiness = true;
  Furble::Host::LumixVirtualCamera gatedPeer(gated);
  NimBLEDevice::setMockPeer(&gatedPeer);
  const auto gatedAdvertisement = gatedPeer.advertisement();
  Furble::Lumix gatedCamera(&gatedAdvertisement);
  passed &= check(gatedCamera.connect(Furble::TxPower::P3, 2000),
                  "LUMIX gated body connects with control readiness only");
  gatedPeer.clearEvents();
  gatedCamera.updateGeoData(gps, time);
  passed &= check(gatedPeer.lastGps().empty(), "LUMIX GPS is withheld before location readiness");
  passed &= check(gatedPeer.lastClock().empty(), "LUMIX clock is withheld before clock readiness");
  passed &= check(gatedPeer.emitLocationReadiness(0x01),
                  "the peer delivers the location readiness notification");
  passed &=
      check(gatedPeer.emitClockRequest(0x01), "the peer delivers the clock request notification");
  gatedCamera.updateGeoData(gps, time);
  passed &= check(gatedPeer.lastGps().size() == 16,
                  "LUMIX GPS is written once location readiness arrives");
  passed &= check(gatedPeer.lastClock().size() == 10,
                  "LUMIX clock is written once clock readiness arrives");

  // The clock request is independent of the location gate. Stop location
  // updates, then request the clock again: the clock packet must still go out
  // while the GPS write stays withheld.
  gatedPeer.clearEvents();
  passed &= check(gatedPeer.emitLocationReadiness(0x02), "the peer stops location updates");
  gatedCamera.updateGeoData(gps, time);
  passed &= check(!hasGpsWrite(gatedPeer.writes()),
                  "LUMIX GPS stays withheld while location updates are stopped");
  passed &=
      check(hasClockWrite(gatedPeer.writes()), "LUMIX clock is written without a location update");

  // 03 asks the camera for the stored packet. It must reopen the gate, not
  // close it: the next tick writes the fix again.
  passed &=
      check(gatedPeer.emitLocationReadiness(0x03), "the peer requests the stored location packet");
  gatedPeer.clearEvents();
  gatedCamera.updateGeoData(gps, time);
  passed &= check(hasGpsWrite(gatedPeer.writes()),
                  "LUMIX 03 reopens the location gate and resends the fix");

  // A stop that arrives after 03 but before the next GPS tick is the newer
  // instruction: it must cancel the pending resend instead of letting it
  // bypass the closed gate.
  passed &= check(gatedPeer.emitLocationReadiness(0x03),
                  "the peer requests the stored location packet again");
  passed &= check(gatedPeer.emitLocationReadiness(0x02),
                  "the peer stops location updates before the next tick");
  gatedPeer.clearEvents();
  gatedCamera.updateGeoData(gps, time);
  passed &=
      check(!hasGpsWrite(gatedPeer.writes()), "a 02 after 03 clears the pending location resend");
  gatedCamera.disconnect();

  NimBLEDevice::resetMock();

  // A body that exposes the control readiness gate but never opens it must fail
  // the connection instead of sending Start out of order.
  Furble::Host::LumixVirtualCamera::Config noControlReadiness;
  noControlReadiness.control_readiness = false;
  Furble::Host::LumixVirtualCamera noReadinessPeer(noControlReadiness);
  NimBLEDevice::setMockPeer(&noReadinessPeer);
  const auto noReadinessAdvertisement = noReadinessPeer.advertisement();
  Furble::Lumix noReadinessCamera(&noReadinessAdvertisement);
  passed &= check(!noReadinessCamera.connect(Furble::TxPower::P3, 5000),
                  "LUMIX connect fails when control readiness never arrives");
  passed &= check(!noReadinessPeer.sessionStarted(),
                  "LUMIX never writes Start without control readiness");

  NimBLEDevice::resetMock();
  if (!passed) {
    return 1;
  }
  std::printf("LUMIX session handshake passed\n");
  return 0;
}
