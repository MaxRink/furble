#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "AdvertisementProtocol.h"
#include "FujifilmProtocol.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using Matcher = bool (*)(const uint8_t *, size_t);

bool matchesNikon(const uint8_t *data, size_t bytes) {
  return Furble::AdvertisementProtocol::matchesNikonReconnect(data, bytes, 0x12345678);
}
bool matchesSony(const uint8_t *data, size_t bytes) {
  return Furble::AdvertisementProtocol::matchesSonyAdvertisement(data, bytes);
}
bool matchesLumix(const uint8_t *data, size_t bytes) {
  return Furble::AdvertisementProtocol::matchesLumixAdvertisement(data, bytes, true);
}
bool matchesDji(const uint8_t *data, size_t bytes) {
  return Furble::AdvertisementProtocol::matchesDJIAdvertisement(data, bytes);
}

struct MatcherCase {
  const char *name;
  Matcher matcher;
};

constexpr std::array<MatcherCase, 4> MATCHERS = {
    {{"Nikon", matchesNikon}, {"Sony", matchesSony}, {"Lumix", matchesLumix}, {"DJI", matchesDji}}
};

struct AdvertisementCase {
  const char *name;
  std::array<uint8_t, 13> data;
  size_t owner;
};

void testDirectedAdvertisementMatrix() {
  // Keep accepted bytes unchanged. Nikon, Lumix, and DJI tolerate trailing bytes.
  const std::array<AdvertisementCase, 4> cases = {
      {
       {"Nikon padded",
           {0x99, 0x03, 0x78, 0x56, 0x34, 0x12, 0x00, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5},
           0},
       {"Sony",
           {0x2d, 0x01, 0x03, 0x00, 0x01, 0x00, 0x34, 0x12, 0x01, 0xc2, 0x00, 0x01, 0x00},
           1},
       {"Lumix padded",
           {0x3a, 0x00, 0x07, 0x10, 0x20, 0x30, 0x40, 0x50, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4},
           2},
       {"DJI padded",
           {0xaa, 0x08, 0x00, 0x00, 0xfa, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7},
           3},
       }
  };

  for (const auto &advertisement : cases) {
    for (size_t index = 0; index < MATCHERS.size(); ++index) {
      const bool actual =
          MATCHERS[index].matcher(advertisement.data.data(), advertisement.data.size());
      const bool expected = index == advertisement.owner;
      if (actual != expected) {
        std::cerr << "FAIL: " << advertisement.name << " is classified as " << MATCHERS[index].name
                  << " (expected " << (expected ? "owner" : "other") << ")\n";
        ++failures;
      }
    }
  }
}

void testFujifilmIsolationAndLengthGates() {
  const std::array<uint8_t, 7> basic = {0xd8, 0x04, 0x02, 0x10, 0x20, 0x30, 0x40};
  const std::array<uint8_t, 8> secure = {0xd8, 0x04, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  std::array<uint8_t, 13> paddedBasic = {0};
  std::array<uint8_t, 13> paddedSecure = {0};
  std::copy(basic.begin(), basic.end(), paddedBasic.begin());
  std::copy(secure.begin(), secure.end(), paddedSecure.begin());
  paddedBasic[7] = 0xf0;
  paddedSecure[8] = 0xf1;

  check(
      Furble::FujifilmProtocol::matchesBasicAdvertisement(basic.data(), basic.size(), true, false),
      "Fujifilm Basic fixture is accepted");
  check(Furble::FujifilmProtocol::matchesSecureAdvertisement(secure.data(), secure.size(), true),
        "Fujifilm Secure fixture is accepted");
  check(!Furble::FujifilmProtocol::matchesBasicAdvertisement(paddedBasic.data(), paddedBasic.size(),
                                                             true, false),
        "Fujifilm Basic padded record is rejected by its exact-length gate");
  check(!Furble::FujifilmProtocol::matchesSecureAdvertisement(paddedSecure.data(),
                                                              paddedSecure.size(), true),
        "Fujifilm Secure padded record is rejected by its exact-length gate");

  for (const auto &matcher : MATCHERS) {
    check(!matcher.matcher(paddedBasic.data(), paddedBasic.size()),
          "padded Fujifilm Basic record is isolated from other vendor matchers");
    check(!matcher.matcher(paddedSecure.data(), paddedSecure.size()),
          "padded Fujifilm Secure record is isolated from other vendor matchers");
  }
}

void testRicohNames() {
  check(Furble::AdvertisementProtocol::matchesRicohName("PENTAX K-3 III"),
        "Pentax name dispatches to Ricoh Imaging");
  check(Furble::AdvertisementProtocol::matchesRicohName("Ricoh GR IIIx"),
        "Ricoh name dispatches to Ricoh Imaging");
  check(!Furble::AdvertisementProtocol::matchesRicohName("Sony Alpha"),
        "Sony name does not dispatch to Ricoh Imaging");
  check(!Furble::AdvertisementProtocol::matchesRicohName("DJI Osmo"),
        "DJI name does not dispatch to Ricoh Imaging");
  check(!Furble::AdvertisementProtocol::matchesRicohName(""),
        "empty camera name is rejected by Ricoh dispatch");
}

void testMalformedAndNullInput() {
  const std::array<uint8_t, 2> truncated = {0xaa, 0x08};
  check(!matchesDji(truncated.data(), truncated.size()), "truncated DJI data is rejected");
  check(!matchesSony(truncated.data(), truncated.size()), "truncated Sony data is rejected");
  check(!matchesLumix(truncated.data(), truncated.size()), "truncated Lumix data is rejected");
  check(!matchesNikon(nullptr, 7), "null Nikon data is rejected");
  check(!matchesSony(nullptr, 13), "null Sony data is rejected");
  check(!matchesLumix(nullptr, 8), "null Lumix data is rejected");
  check(!matchesDji(nullptr, 5), "null DJI data is rejected");
  check(!Furble::FujifilmProtocol::matchesBasicAdvertisement(nullptr, 7, true, false),
        "null Fujifilm Basic data is rejected");
  check(!Furble::FujifilmProtocol::matchesSecureAdvertisement(nullptr, 8, true),
        "null Fujifilm Secure data is rejected");
}

}  // namespace

int main() {
  testDirectedAdvertisementMatrix();
  testFujifilmIsolationAndLengthGates();
  testRicohNames();
  testMalformedAndNullInput();
  if (failures != 0) {
    std::cerr << "vendor protocol conformance: " << failures << " FAILED\n";
    return 1;
  }
  std::cout << "vendor protocol conformance: PASS\n";
  return 0;
}
