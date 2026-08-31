#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "ricoh-model-safety-test";

namespace {

bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool rejectedWithoutWrites(Furble::Host::RicohVirtualCamera::Config config,
                           const char *message,
                           bool expectAssociation,
                           bool expectRoute) {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  const bool initiallyBonded = config.camera_bonded;
  if (initiallyBonded) {
    NimBLEDevice::setBonded(true);
  }
  Furble::Host::RicohVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Ricoh camera(&advertisement);
  const bool rejected = !camera.connect(ESP_PWR_LVL_P3, 1000);
  bool passed = check(rejected, message)
                && check(Furble::Ricoh::matches(&advertisement) == expectRoute,
                         "Ricoh dispatch follows the model identity gate")
                && check(peer.writes().empty(), "rejected Ricoh profile emits no GATT writes")
                && check(NimBLEDevice::isBonded(config.address) == initiallyBonded
                             && peer.cameraBonded() == initiallyBonded,
                         "rejected profile leaves both bond stores unchanged");
  if (expectAssociation) {
    passed &= check(peer.associationConfirmations() == 1,
                    "new pairing drives explicit numeric confirmation");
    passed &= check(
        NimBLEDevice::confirmPasskeyCount() == 1 && !NimBLEDevice::lastConfirmPasskeyAccepted(),
        "production rejects unconfirmed numeric comparison");
  } else {
    passed &=
        check(peer.associationConfirmations() == 0 && NimBLEDevice::confirmPasskeyCount() == 0,
              "unsupported profile is rejected before security side effects");
  }
  return passed;
}

}  // namespace

int main() {
  bool passed = true;

  Furble::Host::RicohVirtualCamera::Config supported;
  supported.advertisement_name = "RICOH GR IV";
  supported.gatt_model = "RICOH GR IV";
  supported.camera_bonded = true;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::RicohVirtualCamera supportedPeer(supported);
  NimBLEDevice::setMockPeer(&supportedPeer);
  NimBLEDevice::setBonded(true);
  const auto supportedAdvertisement = supportedPeer.advertisement();
  Furble::Ricoh supportedCamera(&supportedAdvertisement);
  passed &= check(supportedCamera.connect(ESP_PWR_LVL_P3, 1000),
                  "synthetic incident fixture reaches its compatibility path");
  supportedPeer.clearEvents();
  supportedCamera.shutterPress();
  passed &= check(supportedPeer.writes().size() == 2,
                  "only the synthetic incident fixture reaches compatibility writes");

  supportedPeer.clearEvents();
  Furble::Camera::gps_t gps {12.25, -45.5, 123.75, 9};
  Furble::Camera::timesync_t utc {2026, 8, 30, 14, 15, 16, 99};
  supportedCamera.updateGeoData(gps, utc);
  passed &= check(supportedPeer.writes().size() == 1,
                  "GPS updates do not write the unverified Location Control characteristic");
  if (!supportedPeer.writes().empty()) {
    passed &= check(supportedPeer.writes().front().payload.size() == 32,
                    "GPS retains the prior 32-byte behavior as uncertified");
  }
  supportedPeer.clearEvents();
  utc.second = 60;
  supportedCamera.updateGeoData(gps, utc);
  passed &= check(supportedPeer.writes().empty(),
                  "GPS rejects second 60 outside its retained uncertified range");
  supportedCamera.disconnect();

  Furble::Host::RicohVirtualCamera::Config postConnectMalformed = supported;
  postConnectMalformed.operation_mode_malformed_after_initial = true;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::RicohVirtualCamera postConnectPeer(postConnectMalformed);
  NimBLEDevice::setMockPeer(&postConnectPeer);
  NimBLEDevice::setBonded(true);
  const auto postConnectAdvertisement = postConnectPeer.advertisement();
  Furble::Ricoh postConnectCamera(&postConnectAdvertisement);
  passed &= check(postConnectCamera.connect(ESP_PWR_LVL_P3, 1000),
                  "one-byte initial OperationMode permits a connected session");
  postConnectPeer.clearEvents();
  postConnectCamera.shutterPress();
  passed &= check(postConnectPeer.writes().empty(),
                  "multi-byte per-shutter OperationMode rejects all control writes");
  passed &= check(postConnectPeer.notifications().empty(),
                  "multi-byte per-shutter OperationMode has no physical outcome notification");
  postConnectCamera.disconnect();

  Furble::Host::RicohVirtualCamera::Config newPairing = supported;
  newPairing.camera_bonded = false;
  passed &= rejectedWithoutWrites(
      newPairing, "new GR IV pairing is unsupported without an app decision", true, true);

  Furble::Host::RicohVirtualCamera::Config gr3;
  gr3.advertisement_name = "RICOH GR III";
  gr3.gatt_model = "RICOH GR III";
  passed &= rejectedWithoutWrites(gr3, "GR III is discovery-only", false, false);

  Furble::Host::RicohVirtualCamera::Config pentax;
  pentax.advertisement_name = "PENTAX K-3 Mark III";
  pentax.gatt_model = "PENTAX K-3 Mark III";
  pentax.profile = Furble::Host::RicohVirtualCamera::Profile::PENTAX_K3_III;
  passed &= rejectedWithoutWrites(pentax, "Pentax K-3 III is discovery-only", false, false);

  Furble::Host::RicohVirtualCamera::Config mono;
  mono.advertisement_name = "PENTAX K-3 Mark III Monochrome";
  mono.gatt_model = "PENTAX K-3 Mark III Monochrome";
  mono.profile = Furble::Host::RicohVirtualCamera::Profile::PENTAX_K3_III_MONO;
  passed &=
      rejectedWithoutWrites(mono, "Pentax K-3 III Monochrome is discovery-only", false, false);

  Furble::Host::RicohVirtualCamera::Config hdf;
  hdf.advertisement_name = "GR_H264457";
  hdf.gatt_model = "RICOH GR IV HDF";
  hdf.profile = Furble::Host::RicohVirtualCamera::Profile::GR_IV_HDF;
  passed &= rejectedWithoutWrites(hdf, "GR IV HDF is discovery-only", false, false);

  Furble::Host::RicohVirtualCamera::Config mismatchedModel = supported;
  mismatchedModel.gatt_model = "RICOH GR IV HDF";
  passed &= rejectedWithoutWrites(mismatchedModel,
                                  "advertisement and GATT model mismatch is rejected", false, true);

  Furble::Host::RicohVirtualCamera::Config generic;
  generic.advertisement_name = "RICOH";
  generic.gatt_model = "RICOH";
  passed &= rejectedWithoutWrites(generic, "generic Ricoh name is discovery-only", false, false);

  Furble::Host::RicohVirtualCamera::Config inventedAlias;
  inventedAlias.advertisement_name = "GR IV";
  inventedAlias.gatt_model = "GR IV";
  passed &=
      rejectedWithoutWrites(inventedAlias, "invented GR IV alias is not routed", false, false);

  Furble::Host::RicohVirtualCamera::Config missingModel;
  missingModel.expose_model = false;
  missingModel.camera_bonded = true;
  passed &= rejectedWithoutWrites(missingModel, "missing model is rejected", false, true);

  Furble::Host::RicohVirtualCamera::Config missingMode;
  missingMode.expose_operation_mode = false;
  missingMode.camera_bonded = true;
  passed &= rejectedWithoutWrites(missingMode, "missing OperationMode is rejected", false, true);

  Furble::Host::RicohVirtualCamera::Config unreadableMode;
  unreadableMode.operation_mode_read_fails = true;
  unreadableMode.camera_bonded = true;
  passed &=
      rejectedWithoutWrites(unreadableMode, "unreadable OperationMode is rejected", false, true);

  Furble::Host::RicohVirtualCamera::Config malformedMode;
  malformedMode.operation_mode_malformed = true;
  malformedMode.camera_bonded = true;
  passed &= rejectedWithoutWrites(
      malformedMode, "malformed OperationMode is rejected before connected", false, true);

  return passed ? 0 : 1;
}
