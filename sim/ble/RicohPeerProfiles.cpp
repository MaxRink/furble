#include "RicohPeerProfiles.h"

#include <array>
#include <string>

namespace Furble {
namespace Sim {
namespace Ble {
namespace {

constexpr const char *INFO_SERVICE = "9a5ed1c5-74cc-4c50-b5b6-66a48e7ccff1";
constexpr const char *MODEL_CHARACTERISTIC = "35fe6272-6aa5-44d9-88e1-f09427f51a71";

const char *advertisementName(RicohProfileKind kind) {
  switch (kind) {
    case RicohProfileKind::GR_IV:
      return "RICOH GR IV";
    case RicohProfileKind::GR_IV_HDF:
      return "GR_H264457";
    case RicohProfileKind::PENTAX_K3_III:
      return "PENTAX K-3 Mark III";
    case RicohProfileKind::PENTAX_K3_III_MONO:
      return "PENTAX K-3 Mark III Monochrome";
    case RicohProfileKind::UNKNOWN:
      return "RICOH unknown";
  }
  return "RICOH unknown";
}

const char *gattModel(RicohProfileKind kind) {
  switch (kind) {
    case RicohProfileKind::GR_IV:
      return "RICOH GR IV";
    case RicohProfileKind::GR_IV_HDF:
      return "RICOH GR IV HDF";
    case RicohProfileKind::PENTAX_K3_III:
      return "PENTAX K-3 Mark III";
    case RicohProfileKind::PENTAX_K3_III_MONO:
      return "PENTAX K-3 Mark III Monochrome";
    case RicohProfileKind::UNKNOWN:
      return "RICOH unknown";
  }
  return "RICOH unknown";
}

PeerProfile makeDiscoveryProfile(RicohProfileKind kind) {
  PeerProfile profile;
  profile.advertisement.identity.current.type = AddressType::PUBLIC;
  profile.advertisement.identity.current.bytes = {
      0x52, 0x49, 0x43, 0x4f, 0x48, static_cast<uint8_t>(0x40 + static_cast<size_t>(kind))};
  profile.advertisement.name = advertisementName(kind);
  profile.advertisement.serviceUuids = {INFO_SERVICE};
  profile.supportsMtuExchange = false;
  profile.supportsPhyUpdate = false;
  profile.supportsConnectionParams = false;

  Service info;
  info.uuid = INFO_SERVICE;
  info.startHandle = 1;
  info.endHandle = 4;
  Characteristic model;
  model.uuid = MODEL_CHARACTERISTIC;
  model.declarationHandle = 2;
  model.valueHandle = 3;
  model.properties = PROPERTY_READ;
  model.permissions = PERMISSION_READ;
  const std::string modelValue = gattModel(kind);
  model.value.assign(modelValue.begin(), modelValue.end());
  info.characteristics.push_back(model);
  profile.services.push_back(info);
  return profile;
}

}  // namespace

const PeerProfile &ricohProfile(RicohProfileKind kind) {
  static const std::array<PeerProfile, 5> profiles = {
      makeDiscoveryProfile(RicohProfileKind::GR_IV),
      makeDiscoveryProfile(RicohProfileKind::GR_IV_HDF),
      makeDiscoveryProfile(RicohProfileKind::PENTAX_K3_III),
      makeDiscoveryProfile(RicohProfileKind::PENTAX_K3_III_MONO),
      makeDiscoveryProfile(RicohProfileKind::UNKNOWN),
  };
  const size_t index = static_cast<size_t>(kind);
  return profiles[index < profiles.size() ? index : static_cast<size_t>(RicohProfileKind::UNKNOWN)];
}

}  // namespace Ble
}  // namespace Sim
}  // namespace Furble
