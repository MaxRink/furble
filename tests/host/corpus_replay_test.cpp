#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "GoldenCapture.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "furble-corpus";

namespace {

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

bool parseAddress(const std::string &value, uint64_t &address) {
  if (value.size() != 12) {
    return false;
  }
  address = 0;
  for (const char character : value) {
    uint8_t digit;
    if ((character >= '0') && (character <= '9')) {
      digit = static_cast<uint8_t>(character - '0');
    } else if ((character >= 'a') && (character <= 'f')) {
      digit = static_cast<uint8_t>(character - 'a' + 10);
    } else if ((character >= 'A') && (character <= 'F')) {
      digit = static_cast<uint8_t>(character - 'A' + 10);
    } else {
      return false;
    }
    address = (address << 4) | digit;
  }
  return true;
}

std::vector<uint8_t> geotagPayload() {
  const auto appendInt32 = [](std::vector<uint8_t> &value, int32_t number) {
    const uint32_t bits = static_cast<uint32_t>(number);
    value.push_back(static_cast<uint8_t>(bits));
    value.push_back(static_cast<uint8_t>(bits >> 8));
    value.push_back(static_cast<uint8_t>(bits >> 16));
    value.push_back(static_cast<uint8_t>(bits >> 24));
  };

  std::vector<uint8_t> value;
  appendInt32(value, 525200123);
  appendInt32(value, 134049540);
  appendInt32(value, 34);
  value.insert(value.end(), 4, 0);
  value.insert(value.end(), {0xea, 0x07, 0x08, 0x11, 0x0c, 0x22, 0x38});
  return value;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: corpus_replay_test CAPTURE\n";
    return 2;
  }

  Furble::Host::GoldenCapture capture;
  std::string error;
  if (!Furble::Host::loadGoldenCapture(argv[1], capture, error)) {
    std::cerr << "FAIL: " << error << '\n';
    return 1;
  }
  if (!check(capture.schema == 1, "capture schema is version 1")
      || !check(capture.value("camera") == "x100vi", "capture identifies the X100VI target")
      || !check(capture.value("source") == "synthetic", "fixture is marked synthetic")) {
    return 1;
  }

  std::string name;
  uint64_t address_value = 0;
  std::vector<uint8_t> manufacturer;
  std::vector<NimBLEUUID> advertised_services;
  for (const auto &event : capture.events) {
    if ((event.type != Furble::Host::GoldenEventType::ADVERTISEMENT)) {
      continue;
    }
    if (event.field == "name") {
      name = event.text;
    } else if (event.field == "address") {
      if (!parseAddress(event.text, address_value)) {
        std::cerr << "FAIL: invalid advertisement address\n";
        return 1;
      }
    } else if (event.field == "manufacturer") {
      if (!Furble::Host::decodeHex(event.text, manufacturer, error)) {
        std::cerr << "FAIL: " << error << '\n';
        return 1;
      }
    } else if (event.field == "service") {
      advertised_services.emplace_back(event.service);
    }
  }

  if (!check(!name.empty(), "capture contains an advertisement name")
      || !check(manufacturer.size() == 7, "capture contains a Fujifilm token advertisement")
      || !check(advertised_services.size() == 1, "capture contains one advertised service")) {
    return 1;
  }

  Furble::Host::FujifilmVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address_value, 0);
  std::copy(manufacturer.begin() + 3, manufacturer.end(), config.token.begin());
  config.advertised_services = advertised_services;

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  if (!check(Furble::FujifilmBasic::matches(&advertisement),
             "the replay advertisement matches Fujifilm Basic")) {
    return 1;
  }

  bool connected = false;
  bool disconnected = false;
  {
    Furble::FujifilmBasic client(&advertisement);
    for (const auto &event : capture.events) {
      switch (event.type) {
        case Furble::Host::GoldenEventType::ADVERTISEMENT:
        case Furble::Host::GoldenEventType::SUBSCRIBE:
          break;
        case Furble::Host::GoldenEventType::CONNECT:
          connected = client.connect(ESP_PWR_LVL_P3, 1000);
          break;
        case Furble::Host::GoldenEventType::NOTIFY:
          if (!check(
                  peer.emitNotification(NimBLEUUID(event.service), NimBLEUUID(event.characteristic),
                                        event.payload, event.indication),
                  "captured notification is accepted by the subscribed client")) {
            return 1;
          }
          if (event.characteristic == peer.geotagRequestCharacteristicUUID().toString()) {
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
          }
          break;
        case Furble::Host::GoldenEventType::WRITE:
          break;
        case Furble::Host::GoldenEventType::DISCONNECT:
          client.disconnect();
          disconnected = true;
          break;
      }
    }

    if (!check(connected, "capture connects the client")
        || !check(peer.tokenAccepted(), "replayed pairing token is accepted")
        || !check(peer.configured(), "replayed configuration notification is observed")
        || !check(disconnected, "capture disconnects the client")) {
      return 1;
    }

    for (const auto &event : capture.events) {
      if (event.type != Furble::Host::GoldenEventType::WRITE) {
        continue;
      }
      const std::string service = event.service;
      const std::string characteristic = event.characteristic;
      const bool found =
          std::any_of(peer.writes().begin(), peer.writes().end(), [&](const auto &actual) {
            return (actual.service == service) && (actual.characteristic == characteristic)
                   && (actual.payload == event.payload);
          });
      if (!check(found, "captured write is reproduced by the production client")) {
        return 1;
      }
    }
    if (!check(peer.lastGeotag() == geotagPayload(), "captured geotag write is reproduced")) {
      return 1;
    }
  }

  NimBLEDevice::resetMock();
  std::cout << "golden capture replay: PASS\n";
  return 0;
}
