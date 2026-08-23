#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "AdvertisementProtocol.h"
#include "Blowfish.h"
#include "CameraListProtocol.h"
#include "FujifilmProtocol.h"

namespace {

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::cerr << "  check failed at line " << __LINE__ << ": " << #condition << '\n'; \
      return false;                                                                     \
    }                                                                                   \
  } while (false)

using Furble::CameraListProtocol::IndexEntry;

IndexEntry makeIndexEntry(const char *name, uint32_t type) {
  IndexEntry entry = {};
  std::strncpy(entry.name, name, sizeof(entry.name) - 1);
  entry.type = type;
  return entry;
}

bool testFujifilmAdvertisementParsing() {
  const std::array<uint8_t, 3> base = {0xd8, 0x04, 0x00};
  CHECK(Furble::FujifilmProtocol::isFujifilmAdvertisement(base.data(), base.size()));

  const std::array<uint8_t, 2> shortData = {0xd8, 0x04};
  CHECK(!Furble::FujifilmProtocol::isFujifilmAdvertisement(shortData.data(), shortData.size()));

  const std::array<uint8_t, 3> wrongCompany = {0xd9, 0x04, 0x00};
  CHECK(
      !Furble::FujifilmProtocol::isFujifilmAdvertisement(wrongCompany.data(), wrongCompany.size()));

  const std::array<uint8_t, 7> basicData = {0xd8, 0x04, 0x02, 0x10, 0x20, 0x30, 0x40};
  Furble::FujifilmProtocol::BasicAdvertisement basic = {};
  CHECK(
      Furble::FujifilmProtocol::parseBasicAdvertisement(basicData.data(), basicData.size(), basic));
  CHECK((basic.token == std::array<uint8_t, 4> {0x10, 0x20, 0x30, 0x40}));
  CHECK(Furble::FujifilmProtocol::matchesBasicAdvertisement(basicData.data(), basicData.size(),
                                                            true, false));
  CHECK(Furble::FujifilmProtocol::matchesBasicAdvertisement(basicData.data(), basicData.size(),
                                                            false, true));
  CHECK(!Furble::FujifilmProtocol::matchesBasicAdvertisement(basicData.data(), basicData.size(),
                                                             false, false));

  const std::array<uint8_t, 7> wrongBasicType = {0xd8, 0x04, 0x03, 0x10, 0x20, 0x30, 0x40};
  CHECK(!Furble::FujifilmProtocol::parseBasicAdvertisement(wrongBasicType.data(),
                                                           wrongBasicType.size(), basic));

  const std::array<uint8_t, 8> basicWithExtra = {0xd8, 0x04, 0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
  CHECK(!Furble::FujifilmProtocol::parseBasicAdvertisement(basicWithExtra.data(),
                                                           basicWithExtra.size(), basic));

  const std::array<uint8_t, 8> secureData = {0xd8, 0x04, 0x99, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0};
  Furble::FujifilmProtocol::SecureAdvertisement secure = {};
  CHECK(Furble::FujifilmProtocol::parseSecureAdvertisement(secureData.data(), secureData.size(),
                                                           secure));
  CHECK((secure.serial == std::array<uint8_t, 5> {0xa0, 0xb0, 0xc0, 0xd0, 0xe0}));
  CHECK(Furble::FujifilmProtocol::matchesSecureAdvertisement(secureData.data(), secureData.size(),
                                                             true));
  CHECK(!Furble::FujifilmProtocol::matchesSecureAdvertisement(secureData.data(), secureData.size(),
                                                              false));

  const std::array<uint8_t, 7> shortSecure = {0xd8, 0x04, 0x99, 0xa0, 0xb0, 0xc0, 0xd0};
  CHECK(!Furble::FujifilmProtocol::parseSecureAdvertisement(shortSecure.data(), shortSecure.size(),
                                                            secure));
  CHECK(!Furble::FujifilmProtocol::parseBasicAdvertisement(nullptr, 0, basic));
  return true;
}

bool testFujifilmMessages() {
  const auto press =
      Furble::FujifilmProtocol::makeShutterFrame(Furble::FujifilmProtocol::ShutterAction::PRESS);
  const auto release =
      Furble::FujifilmProtocol::makeShutterFrame(Furble::FujifilmProtocol::ShutterAction::RELEASE);
  const auto focus =
      Furble::FujifilmProtocol::makeShutterFrame(Furble::FujifilmProtocol::ShutterAction::FOCUS);
  const std::array<uint8_t, 2> command = {0x01, 0x00};
  CHECK(press.command == command);
  CHECK(release.command == command);
  CHECK(focus.command == command);
  CHECK((press.parameter == std::array<uint8_t, 2> {0x02, 0x00}));
  CHECK((release.parameter == std::array<uint8_t, 2> {0x00, 0x00}));
  CHECK((focus.parameter == std::array<uint8_t, 2> {0x03, 0x00}));

  const std::array<uint8_t, 2> configured = {0x02, 0x00};
  const std::array<uint8_t, 3> configuredWithExtra = {0x02, 0x00, 0x01};
  const std::array<uint8_t, 2> geotagRequest = {0x01, 0x00};
  CHECK(
      Furble::FujifilmProtocol::isConfigurationNotification(configured.data(), configured.size()));
  CHECK(Furble::FujifilmProtocol::isConfigurationNotification(configuredWithExtra.data(),
                                                              configuredWithExtra.size()));
  CHECK(!Furble::FujifilmProtocol::isConfigurationNotification(geotagRequest.data(),
                                                               geotagRequest.size()));
  CHECK(Furble::FujifilmProtocol::isGeotagRequest(geotagRequest.data(), geotagRequest.size()));
  CHECK(!Furble::FujifilmProtocol::isGeotagRequest(configured.data(), configured.size()));
  CHECK(!Furble::FujifilmProtocol::isGeotagRequest(nullptr, 0));

  const Furble::FujifilmProtocol::GeotagInput input = {1.0, -2.0, -42.9, 2025, 12, 31, 23, 42, 59};
  const auto geotag = Furble::FujifilmProtocol::encodeGeotag(input);
  const std::array<uint8_t, Furble::FujifilmProtocol::GEOTAG_BYTES> expected = {
      0x80, 0x96, 0x98, 0x00, 0x00, 0xd3, 0xce, 0xfe, 0xd6, 0xff, 0xff, 0xff,
      0x00, 0x00, 0x00, 0x00, 0xe9, 0x07, 0x0c, 0x1f, 0x17, 0x2a, 0x3b};
  CHECK(geotag == expected);
  return true;
}

bool testAdvertisementParsing() {
  using Furble::AdvertisementProtocol::LumixAdvertisement;
  using Furble::AdvertisementProtocol::NikonAdvertisement;
  using Furble::AdvertisementProtocol::SonyAdvertisement;

  const std::array<uint8_t, 13> sony = {0x2d, 0x01, 0x03, 0x00, 0x01, 0x00, 0x34,
                                        0x12, 0x01, 0xc2, 0x00, 0x01, 0x00};
  SonyAdvertisement sonyParsed = {};
  CHECK(
      Furble::AdvertisementProtocol::parseSonyAdvertisement(sony.data(), sony.size(), sonyParsed));
  CHECK(sonyParsed.companyID == Furble::AdvertisementProtocol::SONY_COMPANY_ID);
  CHECK(sonyParsed.type == Furble::AdvertisementProtocol::SONY_CAMERA_TYPE);
  CHECK(sonyParsed.model == 0x1234);
  CHECK(sonyParsed.mode22 == 0xc2);
  CHECK(Furble::AdvertisementProtocol::matchesSonyAdvertisement(sony.data(), sony.size()));

  const std::array<uint8_t, 14> sonyOversized = {0x2d, 0x01, 0x03, 0x00, 0x01, 0x00, 0x34,
                                                 0x12, 0x01, 0xc2, 0x00, 0x01, 0x00, 0xff};
  CHECK(Furble::AdvertisementProtocol::matchesSonyAdvertisement(sonyOversized.data(),
                                                                sonyOversized.size()));
  auto sonyWrongMode = sony;
  sonyWrongMode[9] = 0x80;
  CHECK(!Furble::AdvertisementProtocol::matchesSonyAdvertisement(sonyWrongMode.data(),
                                                                 sonyWrongMode.size()));
  auto sonyWrongCompany = sony;
  sonyWrongCompany[0] = 0x2e;
  CHECK(!Furble::AdvertisementProtocol::matchesSonyAdvertisement(sonyWrongCompany.data(),
                                                                 sonyWrongCompany.size()));
  CHECK(!Furble::AdvertisementProtocol::parseSonyAdvertisement(sony.data(), sony.size() - 1,
                                                               sonyParsed));
  CHECK(!Furble::AdvertisementProtocol::parseSonyAdvertisement(nullptr, 0, sonyParsed));

  const std::array<uint8_t, 8> lumix = {0x3a, 0x00, 0x07, 0x10, 0x20, 0x30, 0x40, 0x50};
  LumixAdvertisement lumixParsed = {};
  CHECK(Furble::AdvertisementProtocol::parseLumixAdvertisement(lumix.data(), lumix.size(),
                                                               lumixParsed));
  CHECK(lumixParsed.companyID == Furble::AdvertisementProtocol::PANASONIC_COMPANY_ID);
  CHECK(lumixParsed.flags == 0x07);
  CHECK((lumixParsed.address == std::array<uint8_t, 5> {0x10, 0x20, 0x30, 0x40, 0x50}));
  CHECK(Furble::AdvertisementProtocol::matchesLumixAdvertisement(lumix.data(), lumix.size(), true));
  CHECK(
      !Furble::AdvertisementProtocol::matchesLumixAdvertisement(lumix.data(), lumix.size(), false));
  auto lumixWrongCompany = lumix;
  lumixWrongCompany[0] = 0x3b;
  CHECK(!Furble::AdvertisementProtocol::matchesLumixAdvertisement(lumixWrongCompany.data(),
                                                                  lumixWrongCompany.size(), true));
  const std::array<uint8_t, 9> lumixOversized = {0x3a, 0x00, 0x07, 0x10, 0x20,
                                                 0x30, 0x40, 0x50, 0xff};
  CHECK(Furble::AdvertisementProtocol::matchesLumixAdvertisement(lumixOversized.data(),
                                                                 lumixOversized.size(), true));
  CHECK(!Furble::AdvertisementProtocol::parseLumixAdvertisement(lumix.data(), lumix.size() - 1,
                                                                lumixParsed));
  CHECK(!Furble::AdvertisementProtocol::parseLumixAdvertisement(nullptr, 0, lumixParsed));

  const std::array<uint8_t, 7> nikon = {0x99, 0x03, 0x78, 0x56, 0x34, 0x12, 0x00};
  NikonAdvertisement nikonParsed = {};
  CHECK(Furble::AdvertisementProtocol::parseNikonAdvertisement(nikon.data(), nikon.size(),
                                                               nikonParsed));
  CHECK(nikonParsed.companyID == Furble::AdvertisementProtocol::NIKON_COMPANY_ID);
  CHECK(nikonParsed.device == 0x12345678);
  CHECK(
      Furble::AdvertisementProtocol::matchesNikonReconnect(nikon.data(), nikon.size(), 0x12345678));
  CHECK(!Furble::AdvertisementProtocol::matchesNikonReconnect(nikon.data(), nikon.size(),
                                                              0x12345679));
  auto nikonWrongCompany = nikon;
  nikonWrongCompany[0] = 0x98;
  CHECK(!Furble::AdvertisementProtocol::matchesNikonReconnect(
      nikonWrongCompany.data(), nikonWrongCompany.size(), 0x12345678));
  const std::array<uint8_t, 8> nikonOversized = {0x99, 0x03, 0x78, 0x56, 0x34, 0x12, 0x00, 0xff};
  CHECK(Furble::AdvertisementProtocol::matchesNikonReconnect(nikonOversized.data(),
                                                             nikonOversized.size(), 0x12345678));
  CHECK(!Furble::AdvertisementProtocol::parseNikonAdvertisement(nikon.data(), nikon.size() - 1,
                                                                nikonParsed));
  CHECK(!Furble::AdvertisementProtocol::parseNikonAdvertisement(nullptr, 0, nikonParsed));
  CHECK(Furble::AdvertisementProtocol::matchesNikonDiscovery(false, true));
  CHECK(!Furble::AdvertisementProtocol::matchesNikonDiscovery(true, true));
  CHECK(!Furble::AdvertisementProtocol::matchesNikonDiscovery(false, false));

  const std::array<uint8_t, 5> dji = {0xaa, 0x08, 0x00, 0x00, 0xfa};
  CHECK(Furble::AdvertisementProtocol::matchesDJIAdvertisement(dji.data(), dji.size()));
  const std::array<uint8_t, 6> djiOversized = {0xaa, 0x08, 0x00, 0x00, 0xfa, 0xff};
  CHECK(Furble::AdvertisementProtocol::matchesDJIAdvertisement(djiOversized.data(),
                                                               djiOversized.size()));
  auto djiWrong = dji;
  djiWrong[4] = 0xfb;
  CHECK(!Furble::AdvertisementProtocol::matchesDJIAdvertisement(djiWrong.data(), djiWrong.size()));
  CHECK(!Furble::AdvertisementProtocol::matchesDJIAdvertisement(dji.data(), dji.size() - 1));
  CHECK(!Furble::AdvertisementProtocol::matchesDJIAdvertisement(nullptr, 0));

  CHECK(Furble::AdvertisementProtocol::matchesRicohName("GR"));
  CHECK(Furble::AdvertisementProtocol::matchesRicohName("Ricoh GR IIIx"));
  CHECK(Furble::AdvertisementProtocol::matchesRicohName("Pentax K-3 III"));
  CHECK(Furble::AdvertisementProtocol::matchesRicohName("griii"));
  CHECK(!Furble::AdvertisementProtocol::matchesRicohName("GRX"));
  CHECK(!Furble::AdvertisementProtocol::matchesRicohName("Canon"));
  CHECK(!Furble::AdvertisementProtocol::matchesRicohName(""));
  return true;
}

bool testCameraListPersistence() {
  const IndexEntry first = makeIndexEntry("001122334455", 1);
  const IndexEntry second = makeIndexEntry("AABBCCDDEEFF", 8);
  const std::vector<IndexEntry> source = {first, second};
  std::vector<uint8_t> encoded;
  CHECK(Furble::CameraListProtocol::encodeIndex(source, encoded));
  CHECK(encoded.size() == 2 * Furble::CameraListProtocol::INDEX_ENTRY_BYTES);
  CHECK(std::equal(std::begin(first.name), std::end(first.name), encoded.begin()));
  CHECK(encoded[Furble::CameraListProtocol::INDEX_NAME_BYTES] == 0x01);
  CHECK(encoded[Furble::CameraListProtocol::INDEX_NAME_BYTES + 1] == 0x00);
  CHECK(encoded[Furble::CameraListProtocol::INDEX_NAME_BYTES + 2] == 0x00);
  CHECK(encoded[Furble::CameraListProtocol::INDEX_NAME_BYTES + 3] == 0x00);

  std::vector<IndexEntry> decoded;
  CHECK(Furble::CameraListProtocol::decodeIndex(encoded.data(), encoded.size(), decoded));
  CHECK(decoded.size() == source.size());
  CHECK(std::memcmp(&decoded[0], &source[0], sizeof(IndexEntry)) == 0);
  CHECK(std::memcmp(&decoded[1], &source[1], sizeof(IndexEntry)) == 0);

  std::vector<uint8_t> malformed(encoded.begin(), encoded.end() - 1);
  CHECK(!Furble::CameraListProtocol::decodeIndex(malformed.data(), malformed.size(), decoded));
  CHECK(decoded.empty());
  CHECK(!Furble::CameraListProtocol::decodeIndex(nullptr, 1, decoded));
  CHECK(Furble::CameraListProtocol::decodeIndex(nullptr, 0, decoded));
  CHECK(decoded.empty());

  CHECK(Furble::CameraListProtocol::addressKey(0x112233445566ULL) == "112233445566");

  std::vector<IndexEntry> entries = {first};
  IndexEntry replacement = makeIndexEntry("001122334455", 9);
  Furble::CameraListProtocol::upsertIndex(entries, replacement);
  CHECK(entries.size() == 1);
  CHECK(entries[0].type == 9);
  Furble::CameraListProtocol::upsertIndex(entries, second);
  CHECK(entries.size() == 2);
  CHECK(entries[1].type == 8);
  return true;
}

bool testBlowfishVectors() {
  struct Vector {
    std::vector<uint8_t> key;
    uint32_t inputLeft;
    uint32_t inputRight;
    uint32_t outputLeft;
    uint32_t outputRight;
  };

  const std::vector<Vector> vectors = {
      {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
       0x00000000, 0x00000000,
       0x4ef99745, 0x6198dd78},
      {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
       0xffffffff, 0xffffffff,
       0x51866fd5, 0xb85ecb8a},
      {{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
       0x11111111, 0x11111111,
       0x61f9c380, 0x2281b096},
      {{0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10},
       0x01234567, 0x89abcdef,
       0x0aceab0f, 0xc6a0a28d},
  };

  for (const auto &test : vectors) {
    Furble::Blowfish blowfish(test.key);
    uint32_t left = test.inputLeft;
    uint32_t right = test.inputRight;
    blowfish.encipher(&left, &right);
    CHECK(left == test.outputLeft);
    CHECK(right == test.outputRight);
  }
  return true;
}

}  // namespace

int main() {
  const std::array<std::pair<const char *, bool (*)()>, 5> tests = {
      {
       {"Fujifilm advertisement parsing", testFujifilmAdvertisementParsing},
       {"Fujifilm message framing", testFujifilmMessages},
       {"Cross-vendor advertisement parsing", testAdvertisementParsing},
       {"CameraList persistence", testCameraListPersistence},
       {"Blowfish vectors", testBlowfishVectors},
       }
  };

  for (const auto &test : tests) {
    if (!test.second()) {
      std::cerr << "FAIL " << test.first << '\n';
      return 1;
    }
    std::cout << "PASS " << test.first << '\n';
  }

  std::cout << "All camera protocol tests passed\n";
  return 0;
}
