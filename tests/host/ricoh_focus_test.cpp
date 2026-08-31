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
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::RicohVirtualCamera::Config config;
  config.advertisement_name = "RICOH GR IV";
  config.gatt_model = "RICOH GR IV";
  config.profile = Furble::Host::RicohVirtualCamera::Profile::GR_IV;
  config.camera_bonded = true;
  Furble::Host::RicohVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setBonded(true);
  const auto advertisement = peer.advertisement();
  Furble::Ricoh camera(&advertisement);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000),
             "Ricoh focus regression reaches the connected production path")) {
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
  camera.disconnect();
  return passed ? 0 : 1;
}
