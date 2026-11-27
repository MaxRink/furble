#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "ricoh-sleep-gate-test";

namespace {

constexpr uint8_t MODE_CAPTURE = 0x00;
constexpr uint8_t MODE_BLE_STARTUP = 0x02;

bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

// Capture writes only. A sleeping body now makes the shutter path relink, and
// that reconnect repeats the connect-path identity write, so the journal is not
// capture writes alone. The refusal this test protects is the pair of capture
// writes, never the connect identity write.
std::vector<Furble::Host::RicohVirtualCamera::Write> captureWrites(
    const std::vector<Furble::Host::RicohVirtualCamera::Write> &writes) {
  const std::string flavorUuid =
      Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
  const std::string operationUuid =
      Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  std::vector<Furble::Host::RicohVirtualCamera::Write> filtered;
  for (const auto &write : writes) {
    if ((write.characteristic == flavorUuid) || (write.characteristic == operationUuid)) {
      filtered.push_back(write);
    }
  }
  return filtered;
}

bool checkCaptureWrites(const std::vector<Furble::Host::RicohVirtualCamera::Write> &writes,
                        const char *label) {
  const std::string flavorUuid =
      Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
  const std::string operationUuid =
      Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  const auto capture = captureWrites(writes);
  bool passed = check(capture.size() == 2, label);
  if (capture.size() == 2) {
    passed &= check(
        capture[0].characteristic == flavorUuid && capture[0].payload == std::vector<uint8_t> {0},
        "awake capture selects immediate shooting flavor first");
    passed &= check(capture[1].characteristic == operationUuid
                        && capture[1].payload == std::vector<uint8_t> {1, 1},
                    "awake capture then starts OperationRequest with AF");
  }
  return passed;
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  // The GR IV in BLE standby keeps the link up and still reports CameraPower
  // ON while OperationMode reads BLE_STARTUP. A capture write in that state
  // cold boots the camera and can wedge its firmware, so the shutter path
  // must refuse before touching ShootingFlavor or OperationRequest.
  // Invariant source (power state policy and known issues):
  // https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/power_state_policy.md
  // https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/known_issues.md
  Furble::Host::RicohVirtualCamera::Config config;
  config.camera_power = 0x01;
  config.operation_mode = MODE_BLE_STARTUP;
  Furble::Host::RicohVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Ricoh camera(&advertisement);
  if (!check(camera.connect(Furble::TxPower::P3, 1000),
             "Ricoh sleep gate test reaches the connected production path")) {
    return 1;
  }

  // Asleep at connect time: no capture write at all. This body never leaves
  // standby, so the wake path relinks once and its bounded probe expires
  // without authorizing anything. Plan 149 phase 2 adds the wake; the phase 1
  // invariant it must preserve is that no capture write reaches a body in
  // BLE_STARTUP.
  peer.clearEvents();
  camera.shutterPress();
  bool passed =
      check(captureWrites(peer.writes()).empty(),
            "shutter refused in BLE_STARTUP: no ShootingFlavor or OperationRequest write");
  passed &= check(peer.captureRequestsWhileAsleep() == 0,
                  "no capture request reaches the body while it is in BLE_STARTUP");
  passed &=
      check(peer.reconnectCount() == 1, "the refusal path attempts exactly one wake reconnect");

  // Camera wakes while the connection is held. The connect-time probe cached
  // BLE_STARTUP; only a fresh pre-capture read can see CAPTURE now. This is
  // the stale-cache trap in the wake direction.
  peer.setOperationMode(MODE_CAPTURE);
  peer.clearEvents();
  camera.shutterPress();
  passed &= checkCaptureWrites(peer.writes(),
                               "shutter in CAPTURE emits flavor and OperationRequest writes");

  // Camera drops back into standby on the same held connection. The last
  // fresh read cached CAPTURE; only another fresh read can see BLE_STARTUP.
  // Stale-cache trap in the sleep direction.
  peer.setOperationMode(MODE_BLE_STARTUP);
  peer.clearEvents();
  camera.shutterPress();
  passed &= check(captureWrites(peer.writes()).empty(),
                  "shutter refused again after camera re-enters BLE_STARTUP mid-connection");
  passed &= check(peer.captureRequestsWhileAsleep() == 0,
                  "the second standby refusal writes no capture either");

  // An OperationMode read failure is indistinguishable from an unknown state
  // and must also be a safe refusal.
  peer.setOperationMode(MODE_CAPTURE);
  peer.setOperationModeReadFails(true);
  peer.clearEvents();
  camera.shutterPress();
  passed &=
      check(captureWrites(peer.writes()).empty(), "shutter refused when OperationMode read fails");
  passed &= check(peer.captureRequestsWhileAsleep() == 0,
                  "a failed OperationMode read never writes a capture");

  // Reads recover: capture works again.
  peer.setOperationModeReadFails(false);
  peer.clearEvents();
  camera.shutterPress();
  passed &= checkCaptureWrites(peer.writes(), "shutter recovers once OperationMode reads CAPTURE");

  camera.disconnect();
  return passed ? 0 : 1;
}
