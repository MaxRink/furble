#include <array>
#include <cstdint>
#include <iostream>

#include <NimBLEAdvertisedDevice.h>

#include "Camera.h"
#include "CameraList.h"
#include "DJIOsmo.h"

const char *LOG_TAG = "advertisement-dispatch";

namespace {

#define CHECK(condition)                                                              \
  do {                                                                                \
    if (!(condition)) {                                                               \
      std::cerr << "check failed at line " << __LINE__ << ": " << #condition << '\n'; \
      return false;                                                                   \
    }                                                                                 \
  } while (false)

bool testDispatchAndDeduplication() {
  using Furble::Camera;
  Furble::CameraList::clear();
  CHECK(!Furble::CameraList::match(nullptr));
  CHECK(Furble::CameraList::size() == 0);

  const std::array<uint8_t, 8> lumix = {0x3a, 0x00, 0x07, 0x10, 0x20, 0x30, 0x40, 0x50};
  NimBLEAdvertisedDevice device;
  device.setAddress(NimBLEAddress(0x112233445566ULL));
  device.setManufacturerData(lumix.data(), lumix.size());
  device.addServiceUUID(NimBLEUUID("054ac620-3214-11e6-ac0d-0002a5d5c51b"));

  CHECK(Furble::CameraList::match(&device));
  CHECK(Furble::CameraList::size() == 1);
  CHECK(Furble::CameraList::last()->getType() == Camera::Type::PANASONIC_LUMIX);
  CHECK(!Furble::CameraList::match(&device));
  CHECK(Furble::CameraList::size() == 1);

  NimBLEAdvertisedDevice djiAdvertisement;
  djiAdvertisement.setAddress(NimBLEAddress(0x223344556677ULL));
  djiAdvertisement.setName("DJI Osmo Action 5 Pro");
  auto dji = std::make_shared<Furble::DJIOsmo>(&djiAdvertisement);
  CHECK(dji->getPairType() == Camera::PairType::NEW);
  Furble::CameraList::save(dji);
  CHECK(dji->getPairType() == Camera::PairType::SAVED);

  Furble::CameraList::clear();
  return true;
}

}  // namespace

int main() {
  if (!testDispatchAndDeduplication())
    return 1;
  std::cout << "PASS advertisement dispatch and deduplication\n";
  return 0;
}
