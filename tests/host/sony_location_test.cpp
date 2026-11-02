// Sony location-form regression.
//
// DD21 byte 4 bit 1 selects the 95-byte location packet. A body that exposes
// only the 91-byte form must receive 91 bytes, not 95.

#include <cstdint>
#include <iostream>
#include <vector>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Sony.h"
#include "SonyVirtualCamera.h"

const char *LOG_TAG = "sony-location-test";

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool run(Furble::Host::SonyVirtualCamera &peer,
         Furble::Host::SonyVirtualCamera::LocationForm expected,
         size_t expectedBytes,
         const char *label) {
  NimBLEDevice::resetMock();
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Sony camera(&advertisement);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), label)) {
    return false;
  }
  const Furble::Camera::gps_t gps = {52.5, 13.4, 34.0, 8};
  const Furble::Camera::timesync_t time = {2026, 11, 1, 12, 34, 56, 0};
  camera.updateGeoData(gps, time);
  return check(peer.lastLocationForm() == expected, "the selected location form is used")
         && check(peer.lastLocation().size() == expectedBytes, "the location length is correct");
}

}  // namespace

int main() {
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::SonyVirtualCamera::Config config95;
  config95.timezone_supported = true;
  Furble::Host::SonyVirtualCamera peer95(config95);
  bool passed = run(peer95, Furble::Host::SonyVirtualCamera::LocationForm::Bytes95, 95,
                    "Sony 95-byte location form connects");

  Furble::Host::SonyVirtualCamera::Config config91;
  config91.timezone_supported = false;
  config91.require_91_byte_location = true;
  Furble::Host::SonyVirtualCamera peer91(config91);
  passed &= run(peer91, Furble::Host::SonyVirtualCamera::LocationForm::Bytes91, 91,
                "Sony 91-byte location form connects");

  NimBLEDevice::resetMock();
  return passed ? 0 : 1;
}
