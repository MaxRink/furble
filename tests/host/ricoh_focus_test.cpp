#include <iostream>
#include <string>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "ricoh-focus-test";

namespace {
bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}
}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);
  Furble::Host::RicohVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Ricoh camera(&advertisement);
  if (!check(camera.connect(Furble::TxPower::P3, 1000),
             "Ricoh focus regression reaches the connected production path")) {
    return 1;
  }
  if (!check(peer.pairedDeviceName() == Furble::Device::getStringID(),
             "Ricoh writes the paired-device name on connect")) {
    return 1;
  }

  peer.clearEvents();
  camera.focusPress();
  camera.focusRelease();

  // The Ricoh Shooting service exposes OperationRequest for capture. A focus
  // gesture must not write that characteristic and therefore must not take a
  // photograph. The protocol source lists Operation Request as capture
  // control and Focus Mode as a separate configuration characteristic:
  // https://github.com/dm-zharov/ricoh-gr-bluetooth-api/blob/main/shooting/operation_request.md
  // https://github.com/dm-zharov/ricoh-gr-bluetooth-api/blob/main/shooting/focus_mode.md
  const std::string operationUuid =
      Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  bool passed =
      check(peer.writes().empty(), "Ricoh focus press/release emits no capture operation");

  // A shutter press is the supported Ricoh operation. It selects the immediate
  // shooting flavor, then starts one capture with autofocus. This guards the
  // important distinction from a hypothetical focus-only command: the AF
  // parameter belongs to capture, not to focusPress().
  const std::string flavorUuid =
      Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
  peer.clearEvents();
  camera.shutterPress();
  const auto &writes = peer.writes();
  passed &=
      check(writes.size() == 2, "Ricoh shutter press emits flavor and OperationRequest writes");
  if (writes.size() == 2) {
    passed &= check(
        writes[0].characteristic == flavorUuid && writes[0].payload == std::vector<uint8_t> {0},
        "Ricoh shutter selects immediate shooting flavor");
    passed &= check(writes[1].characteristic == operationUuid
                        && writes[1].payload == std::vector<uint8_t> {1, 1},
                    "Ricoh shutter OperationRequest starts capture with AF");
  }
  // Capture Status confirms the shot. The peer reports 01 00 00 00 on the
  // capture start and 00 00 00 00 on completion. The driver counts only the
  // 1 to 0 transition, so a capture start alone does not imply a photograph.
  passed &= check(peer.capturing(), "Ricoh peer entered the capturing state");
  passed &= check(camera.captureInProgress(), "Ricoh driver decodes the capturing state");
  passed &= check(camera.capturesCompleted() == 0,
                  "Ricoh does not report a completed capture before the transition");
  peer.completeCapture();
  passed &= check(!camera.captureInProgress(), "Ricoh driver clears the capturing state");
  passed &= check(camera.capturesCompleted() == 1,
                  "Ricoh counts exactly one capture on the 1 to 0 transition");
  passed &= check(camera.captureStatus() == 0x00, "Ricoh decodes the raw status byte");

  // Geotagging uses the Camera Service GEO Tag enable characteristic, not the
  // WLAN Control Network Type characteristic. The GPS packet ends with the
  // WGS84 datum selector 0, not a centisecond value. The GEO Tag role is
  // confirmed by firmware and the official app. The datum value is app-static;
  // firmware never reads that byte. See docs/camera-research/ricoh.md.
  const std::string geotagEnableUuid =
      Furble::Host::RicohVirtualCamera::geotagEnableCharacteristicUUID().toString();
  const std::string gpsInfoUuid =
      Furble::Host::RicohVirtualCamera::gpsInformationCharacteristicUUID().toString();
  const Furble::Camera::gps_t gps = {52.5163, 13.3777, 34.5, 9};
  const Furble::Camera::timesync_t timesync = {2026, 11, 1, 12, 34, 56, 78};
  peer.clearEvents();
  camera.updateGeoData(gps, timesync);
  const auto &geoWrites = peer.writes();
  passed &= check(geoWrites.size() == 2, "Ricoh geotag emits enable and GPS writes");
  if (geoWrites.size() == 2) {
    passed &= check(geoWrites[0].characteristic == geotagEnableUuid
                        && geoWrites[0].payload == std::vector<uint8_t> {1},
                    "Ricoh geotag enables the Camera Service GEO Tag characteristic");
    passed &= check(geoWrites[1].characteristic == gpsInfoUuid && geoWrites[1].payload.size() == 32,
                    "Ricoh geotag writes one 32-byte GPS Information packet");
    if (geoWrites[1].payload.size() == 32) {
      passed &= check(geoWrites[1].payload.back() == 0,
                      "Ricoh GPS packet ends with the WGS84 datum selector");
    }
  }

  camera.disconnect();
  return passed ? 0 : 1;
}
