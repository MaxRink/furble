#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-host";

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> value) {
  return {value};
}

void appendInt32(std::vector<uint8_t> &value, int32_t number) {
  const uint32_t bits = static_cast<uint32_t>(number);
  value.push_back(static_cast<uint8_t>(bits));
  value.push_back(static_cast<uint8_t>(bits >> 8));
  value.push_back(static_cast<uint8_t>(bits >> 16));
  value.push_back(static_cast<uint8_t>(bits >> 24));
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();

  if (!check(Furble::FujifilmBasic::matches(&advertisement),
             "the virtual camera advertises as Fujifilm Basic")) {
    return 1;
  }
  if (!check(advertisement.getManufacturerData().size() == 7,
             "the advertisement contains the company, type, and token")) {
    return 1;
  }

  {
    Furble::FujifilmBasic client(&advertisement);
    if (!check(client.connect(ESP_PWR_LVL_P3, 1000), "the Fujifilm client connects")) {
      return 1;
    }
    if (!check(client.isConnected() && peer.connected(), "both sides report a connection")) {
      return 1;
    }
    if (!check(peer.tokenAccepted(), "the advertised token is accepted")) {
      return 1;
    }
    if (!check(peer.identifier() == Furble::Device::getStringID(),
               "the client sends its identifier after the token")) {
      return 1;
    }
    if (!check(peer.configured(), "the server sends the configuration indication")) {
      return 1;
    }
    if (!check(peer.notifications().size() == 1,
               "the configuration notification is delivered through the mock")) {
      return 1;
    }

    peer.clearEvents();
    client.shutterPress();
    client.shutterRelease();
    client.focusPress();
    client.focusRelease();
    if (!check(peer.writes().size() == 8, "shutter and focus use two writes each")) {
      return 1;
    }
    if (!check(peer.writes()[0].payload == bytes({0x01, 0x00}), "shutter writes its command")) {
      return 1;
    }
    if (!check(peer.writes()[1].payload == bytes({0x02, 0x00}), "shutter writes press")) {
      return 1;
    }
    if (!check(peer.writes()[5].payload == bytes({0x03, 0x00}), "focus writes focus")) {
      return 1;
    }

    peer.clearEvents();
    if (!check(peer.requestGeotag(), "the server sends its geotag request notification")) {
      return 1;
    }
    if (!check(peer.geotagRequested(), "the peer records the geotag request")) {
      return 1;
    }

    const Furble::Camera::gps_t gps = {
        .latitude = 52.5200123,
        .longitude = 13.404954,
        .altitude = 34.0,
        .satellites = 8,
    };
    const Furble::Camera::timesync_t time = {
        .year = 2026,
        .month = 8,
        .day = 17,
        .hour = 12,
        .minute = 34,
        .second = 56,
        .centisecond = 0,
    };
    client.updateGeoData(gps, time);

    std::vector<uint8_t> expected;
    appendInt32(expected, 525200123);
    appendInt32(expected, 134049540);
    appendInt32(expected, 34);
    expected.insert(expected.end(), 4, 0);
    expected.push_back(0xea);
    expected.push_back(0x07);
    expected.push_back(0x08);
    expected.push_back(0x11);
    expected.push_back(0x0c);
    expected.push_back(0x22);
    expected.push_back(0x38);
    if (!check(peer.lastGeotag() == expected, "the geotag payload is written byte for byte")) {
      return 1;
    }

    client.disconnect();
    if (!check(!client.isConnected() && !peer.connected(), "disconnect reaches the peer")) {
      return 1;
    }
  }

  NimBLEDevice::resetMock();
  std::cout << "fujifilm camera harness: PASS\n";
  return 0;
}
