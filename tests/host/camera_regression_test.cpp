#include <iostream>
#include <memory>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "Nikon.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"

const char *LOG_TAG = "camera-regression";

namespace {
bool check(bool condition, const char *message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool testRegistrationTimeoutException() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer;
  ble_gap_upd_params registration{};
  registration.itvl_min = 200;
  registration.itvl_max = 240;
  registration.supervision_timeout = 3200;
  peer.requestConnParamsDuringConnect(registration);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmBasic camera(&advertisement);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), "Fujifilm registration completes")) return false;
  if (!check(peer.registrationConnParamsAccepted(),
             "over-cap peer request is accepted during registration")) return false;
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "registration leaves a client")) return false;
  if (!check(!client->mockPeerRequestConnParams(registration),
             "same over-cap request is rejected after registration")) return false;
  camera.shutterPress();
  return check(!peer.writes().empty(), "shutter remains usable after timeout rejection");
}

bool testNullNikonCallbacks() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer;
  const auto advertisement = peer.advertisement();
  if (!check(!Furble::Nikon::matches(nullptr), "Nikon matcher rejects null")) return false;
  Furble::Nikon camera(&advertisement);
  auto *callbacks = static_cast<NimBLEScanCallbacks *>(&camera);
  callbacks->onResult(nullptr);
  return true;
}

bool testRicohBondPolicy() {
  auto run = [](bool saved) {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    Furble::Host::FujifilmVirtualCamera peer;
    peer.setSecureConnectionResult(false);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    std::unique_ptr<Furble::Ricoh> camera;
    if (saved) {
      Furble::Ricoh fresh(&advertisement);
      std::vector<uint8_t> data(fresh.getSerialisedBytes());
      if (!fresh.serialise(data.data(), data.size())) return false;
      camera = std::make_unique<Furble::Ricoh>(data.data(), data.size());
    } else {
      camera = std::make_unique<Furble::Ricoh>(&advertisement);
    }
    NimBLEDevice::setBonded(true);
    camera->connect(ESP_PWR_LVL_P3, 1000);
    return NimBLEDevice::deleteBondCount() == (saved ? 0u : 1u)
           && NimBLEDevice::isBonded(advertisement.getAddress()) == saved;
  };
  return check(run(false), "fresh Ricoh pairing clears only its stale bond")
         && check(run(true), "saved Ricoh reconnect preserves its bond");
}
}  // namespace

int main() {
  return testRegistrationTimeoutException() && testNullNikonCallbacks() && testRicohBondPolicy()
             ? 0
             : 1;
}
