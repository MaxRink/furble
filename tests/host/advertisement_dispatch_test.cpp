#include <array>
#include <cstdint>
#include <iostream>

#include <NimBLEAdvertisedDevice.h>

#include "Camera.h"
#include "CameraList.h"

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

  Furble::CameraList::clear();
  return true;
}

/**
 * A scan result is not a saved camera until it is saved.
 *
 * The connectable list carries both, so the console tells them apart with
 * CameraList::isSaved(). It reads the store, which is what makes it correct
 * for a scan that rediscovers a camera the device already knows.
 */
bool testSavedFlag() {
  Furble::CameraList::clear();

  const std::array<uint8_t, 8> lumix = {0x3a, 0x00, 0x07, 0x10, 0x20, 0x30, 0x40, 0x50};
  NimBLEAdvertisedDevice device;
  device.setAddress(NimBLEAddress(0x112233445566ULL));
  device.setManufacturerData(lumix.data(), lumix.size());
  device.addServiceUUID(NimBLEUUID("054ac620-3214-11e6-ac0d-0002a5d5c51b"));

  CHECK(!Furble::CameraList::isSaved(nullptr));
  CHECK(Furble::CameraList::match(&device));

  const auto camera = Furble::CameraList::last();
  CHECK(!Furble::CameraList::isSaved(camera.get()));

  Furble::CameraList::save(camera.get());
  CHECK(Furble::CameraList::isSaved(camera.get()));
  CHECK(Furble::CameraList::getSaveCount() == 1);

  Furble::CameraList::remove(camera.get());
  CHECK(!Furble::CameraList::isSaved(camera.get()));

  Furble::CameraList::clear();
  return true;
}

}  // namespace

int main() {
  if (!testDispatchAndDeduplication())
    return 1;
  if (!testSavedFlag())
    return 1;
  std::cout << "PASS advertisement dispatch and deduplication\n";
  return 0;
}
