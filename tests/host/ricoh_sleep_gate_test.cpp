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

bool checkCaptureWrites(const std::vector<Furble::Host::RicohVirtualCamera::Write> &writes,
                        const char *label) {
  const std::string flavorUuid =
      Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
  const std::string operationUuid =
      Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  bool passed = check(writes.size() == 2, label);
  if (writes.size() == 2) {
    passed &= check(
        writes[0].characteristic == flavorUuid && writes[0].payload == std::vector<uint8_t> {0},
        "awake capture selects immediate shooting flavor first");
    passed &= check(writes[1].characteristic == operationUuid
                        && writes[1].payload == std::vector<uint8_t> {1, 1},
                    "awake capture then starts OperationRequest with AF");
  }
  return passed;
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  // The GR IV in BLE standby keeps the link up and still reports CameraPower
  // ON while OperationMode reads BLE_STARTUP. A capture write in that state
  // cold boots the camera and can wedge its firmware, so the shutter path
  // must refuse before touching ShootingFlavor or OperationRequest.
  // Invariant source (power state policy and known issues):
  // https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/power_state_policy.md
  // https://github.com/sky18Dragon/RICOH-GR-Live-View-Shooting/blob/main/docs/known_issues.md
  Furble::Host::RicohVirtualCamera::Config config;
  config.advertisement_name = "RICOH GR IV";
  config.gatt_model = "RICOH GR IV";
  config.profile = Furble::Host::RicohVirtualCamera::Profile::GR_IV;
  config.camera_bonded = true;
  config.camera_power = 0x01;
  config.operation_mode = MODE_BLE_STARTUP;
  Furble::Host::RicohVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setBonded(true);
  const auto advertisement = peer.advertisement();
  Furble::Ricoh camera(&advertisement);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000),
             "Ricoh sleep gate test reaches the connected production path")) {
    return 1;
  }

  // Asleep at connect time: no side-effect writes at all.
  peer.clearEvents();
  camera.shutterPress();
  bool passed =
      check(peer.writes().empty(),
            "shutter refused in BLE_STARTUP: no ShootingFlavor or OperationRequest write");

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
  passed &= check(peer.writes().empty(),
                  "shutter refused again after camera re-enters BLE_STARTUP mid-connection");

  // An OperationMode read failure is indistinguishable from an unknown state
  // and must also be a safe refusal.
  peer.setOperationMode(MODE_CAPTURE);
  peer.setOperationModeReadFails(true);
  peer.clearEvents();
  camera.shutterPress();
  passed &= check(peer.writes().empty(), "shutter refused when OperationMode read fails");

  // Reads recover: capture works again.
  peer.setOperationModeReadFails(false);
  peer.clearEvents();
  camera.shutterPress();
  passed &= checkCaptureWrites(peer.writes(), "shutter recovers once OperationMode reads CAPTURE");

  camera.disconnect();
  return passed ? 0 : 1;
}
