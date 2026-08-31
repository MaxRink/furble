#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "RicohPeerProfiles.h"
#include "VirtualBleRuntime.h"

namespace {

using Furble::Sim::Ble::AttStatus;
using Furble::Sim::Ble::PeerProfile;
using Furble::Sim::Ble::Provenance;
using Furble::Sim::Ble::Result;
using Furble::Sim::Ble::ricohProfile;
using Furble::Sim::Ble::RicohProfileKind;
using Furble::Sim::Ble::SyntheticConformancePeer;
using Furble::Sim::Ble::VirtualBleRuntime;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool isUnsupported(const Furble::Sim::Ble::OperationResult &result) {
  return result.result == Result::UNSUPPORTED
         && result.attStatus == AttStatus::REQUEST_NOT_SUPPORTED;
}

}  // namespace

int main() {
  constexpr std::array<RicohProfileKind, 5> kinds = {
      RicohProfileKind::GR_IV,         RicohProfileKind::GR_IV_HDF,
      RicohProfileKind::PENTAX_K3_III, RicohProfileKind::PENTAX_K3_III_MONO,
      RicohProfileKind::UNKNOWN,
  };
  bool passed = true;

  for (const RicohProfileKind kind : kinds) {
    const PeerProfile &profile = ricohProfile(kind);
    passed &= check(profile.services.size() == 1, "Ricoh fixtures expose discovery only");
    passed &= check(profile.services.front().characteristics.size() == 1,
                    "Ricoh fixtures expose only the model characteristic");
    passed &= check(!profile.supportsMtuExchange && !profile.supportsPhyUpdate
                        && !profile.supportsConnectionParams,
                    "uncertified Ricoh fixtures reject unproven controller operations");

    SyntheticConformancePeer peer(profile);
    VirtualBleRuntime runtime;
    const auto advertised = runtime.advertise(peer);
    passed &= check(advertised.ok(), "Ricoh discovery fixture advertises valid raw data");
    passed &=
        check(advertised.provenance == Provenance::SYNTHETIC && !advertised.certificationEligible,
              "Ricoh profile provenance remains uncertified");
    const auto connected = runtime.connect(peer, profile.advertisement.identity.current);
    passed &= check(connected.ok(), "Ricoh discovery fixture accepts a synthetic connection");
    if (!connected.ok()) {
      continue;
    }
    passed &= check(runtime.discover(connected.adapterId).ok(),
                    "Ricoh discovery fixture completes service discovery");
    const auto model = runtime.read(connected.adapterId, 3);
    const std::string modelValue(model.value.begin(), model.value.end());
    passed &= check(model.ok(), "model read succeeds for the discovery fixture");
    if (kind == RicohProfileKind::GR_IV_HDF) {
      passed &= check(profile.advertisement.name == "GR_H264457" && modelValue == "RICOH GR IV HDF"
                          && profile.advertisement.name != modelValue,
                      "HDF scan identity is kept separate from its GATT model");
    } else {
      passed &= check(modelValue == profile.advertisement.name,
                      "model read is bounded to the immutable fixture value");
    }
    const auto write = runtime.write(connected.adapterId, 3, {0x01}, true);
    passed &= check(
        write.result == Result::NOT_PERMITTED && write.attStatus == AttStatus::WRITE_NOT_PERMITTED,
        "discovery-only model characteristic cannot receive writes");
    passed &= check(isUnsupported(runtime.exchangeMtu(connected.adapterId, 100)),
                    "uncertified Ricoh fixture has no invented MTU behavior");
    passed &=
        check(isUnsupported(runtime.updatePhy(connected.adapterId, Furble::Sim::Ble::Phy::LE_2M)),
              "uncertified Ricoh fixture has no invented PHY behavior");
    passed &= check(isUnsupported(runtime.updateConnectionParams(connected.adapterId,
                                                                 profile.connectionParams)),
                    "uncertified Ricoh fixture has no invented controller timing");
    const auto disconnected = runtime.disconnect(connected.adapterId);
    passed &= check(disconnected.ok(), "Ricoh discovery fixture tears down cleanly");
  }
  return passed ? 0 : 1;
}
