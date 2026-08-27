#include <iostream>
#include <memory>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "Nikon.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "camera-regression";

namespace {
bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool testRegistrationTimeoutException() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  camera.setConnSaverEnabled(true);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), "Fujifilm registration completes"))
    return false;
  if (!check(peer.registrationConnParamsAccepted(),
             "over-cap peer request is accepted during registration"))
    return false;
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "registration leaves a client"))
    return false;
  if (!check(client->mockConnInfoReadCount() >= 2,
             "Secure registration waits for the asynchronous parameter update"))
    return false;
  if (!check(!client->mockConnParamUpdatePending(),
             "Secure registration observes the controller-applied parameters"))
    return false;
  ble_gap_upd_params overCap = {};
  overCap.itvl_min = 200;
  overCap.itvl_max = 240;
  overCap.supervision_timeout = 3200;
  if (!check(!client->mockPeerRequestConnParams(overCap),
             "same over-cap request is rejected after registration"))
    return false;
  const NimBLEConnInfo effective = client->getConnInfo();
  if (!check(effective.getConnInterval() >= BLE_GAP_INITIAL_CONN_ITVL_MIN
                 && effective.getConnInterval() <= BLE_GAP_INITIAL_CONN_ITVL_MAX
                 && effective.getConnLatency() == 1
                 && effective.getConnTimeout() == (2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT),
             "exact fast profile is live after secure registration"))
    return false;
  camera.shutterPress();
  if (!check(!peer.writes().empty(), "shutter remains usable after timeout rejection"))
    return false;
  return check(camera.setConnProfile(Furble::Camera::ConnProfile::IDLE),
               "confirmed fast transition releases the peer override for idle");
}

bool testRegistrationFastProfileTimeout() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setConnParamApplyDelayReads(1000);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(!camera.connect(ESP_PWR_LVL_P3, 1000),
               "Secure registration is bounded when FAST never applies");
}

bool testDelayedRegistrationParameterRequest() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  peer.setDelayRegistrationConnParamsUntilFastRequest(true);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(camera.connect(ESP_PWR_LVL_P3, 1000) && peer.registrationConnParamsAccepted(),
               "delayed Secure registration parameters remain inside the narrow gate");
}

bool testNullAndMissingIdentifierBoundaries() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config secureConfig;
  secureConfig.secure = true;
  Furble::Host::FujifilmVirtualCamera securePeer(secureConfig);
  const auto secureAdvertisement = securePeer.advertisement();
  Furble::FujifilmSecure secure(&secureAdvertisement);
  static_cast<NimBLEScanCallbacks *>(&secure)->onResult(nullptr);

  NimBLEDevice::resetMock();
  Furble::Host::FujifilmVirtualCamera basicPeer;
  basicPeer.suppressCharacteristic(
      Furble::Host::FujifilmVirtualCamera::pairServiceUUID(),
      Furble::Host::FujifilmVirtualCamera::identifierCharacteristicUUID());
  NimBLEDevice::setMockPeer(&basicPeer);
  const auto basicAdvertisement = basicPeer.advertisement();
  Furble::FujifilmBasic basic(&basicAdvertisement);
  return check(!basic.connect(ESP_PWR_LVL_P3, 1000),
               "Fujifilm Basic rejects a missing identifier characteristic");
}

bool testNullNikonCallbacks() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer;
  const auto advertisement = peer.advertisement();
  if (!check(!Furble::Nikon::matches(nullptr), "Nikon matcher rejects null"))
    return false;
  Furble::Nikon camera(&advertisement);
  auto *callbacks = static_cast<NimBLEScanCallbacks *>(&camera);
  callbacks->onResult(nullptr);
  return true;
}

bool testSecureRegistrationDropStopsGATT() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.dropLinkOnSubscribe(Furble::Host::FujifilmVirtualCamera::configurationServiceUUID(),
                           Furble::Host::FujifilmVirtualCamera::configurationIndication1UUID());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  if (!check(!camera.connect(ESP_PWR_LVL_P3, 1000),
             "Secure registration aborts when the peer drops during subscription"))
    return false;
  if (!check(!camera.isConnected() && !peer.connected(),
             "mid-registration drop leaves no connected session"))
    return false;
  if (!check(peer.accessAfterDrop() == 0, "mid-registration drop makes no subsequent GATT calls"))
    return false;
  for (const auto &write : peer.writes()) {
    if (!check(write.characteristic
                   != Furble::Host::FujifilmVirtualCamera::shutterCharacteristicUUID().toString(),
               "mid-registration drop does not reach shutter writes"))
      return false;
  }
  return true;
}

bool testRicohBondPolicy() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::RicohVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  NimBLEDevice::setBonded(true);
  auto first = std::make_unique<Furble::Ricoh>(&advertisement);
  if (!check(!first->connect(ESP_PWR_LVL_P3, 1000),
             "stale local Ricoh bond fails fresh pairing once"))
    return false;
  if (!check(peer.cameraBonded() == false && NimBLEDevice::deleteBondCount() == 1u,
             "fresh Ricoh pairing clears only the stale local bond"))
    return false;

  auto retry = std::make_unique<Furble::Ricoh>(&advertisement);
  if (!check(retry->connect(ESP_PWR_LVL_P3, 1000),
             "Ricoh retry performs numeric-comparison pairing"))
    return false;
  retry->shutterPress();
  bool flavor = false;
  bool operation = false;
  for (const auto &write : peer.writes()) {
    flavor |= write.characteristic
              == Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
    operation |=
        write.characteristic
        == Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  }
  if (!check(peer.cameraBonded() && flavor && operation, "Ricoh retry reaches the paired shutter"))
    return false;
  retry->disconnect();

  auto runSavedFailure = [&]() {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    Furble::Host::RicohVirtualCamera::Config config;
    config.camera_bonded = true;
    config.accept_numeric_comparison = false;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    NimBLEDevice::setBonded(true);
    Furble::Ricoh fresh(&advertisement);
    std::vector<uint8_t> data(fresh.getSerialisedBytes());
    if (!fresh.serialise(data.data(), data.size()))
      return false;
    Furble::Ricoh saved(data.data(), data.size());
    if (saved.connect(ESP_PWR_LVL_P3, 1000))
      return false;
    return NimBLEDevice::deleteBondCount() == 0u
           && NimBLEDevice::isBonded(advertisement.getAddress());
  };
  return check(runSavedFailure(), "saved Ricoh reconnect preserves its bond");
}
}  // namespace

int main() {
  return testRegistrationTimeoutException() && testRegistrationFastProfileTimeout()
                 && testDelayedRegistrationParameterRequest()
                 && testNullAndMissingIdentifierBoundaries() && testNullNikonCallbacks()
                 && testSecureRegistrationDropStopsGATT() && testRicohBondPolicy()
             ? 0
             : 1;
}
