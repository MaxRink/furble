// Sony advertisement matcher regression.
//
// A 2019 ILCE body advertises tag 0x22 with bits 7 and 6 set and bit 1 clear
// (0xE8 or 0xEC). Furble required bit 1 as well, so those bodies never reached
// the camera list, pairing or the location service. The matcher now accepts
// both generations and reports which one the advertisement describes, while a
// byte without bits 7 and 6 stays rejected.

#include <cstdint>
#include <iostream>
#include <vector>

#include "protocol/AdvertisementProtocol.h"

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

/**
 * Sony manufacturer data with the tag 0x22 mode byte the caller supplies.
 *
 * Layout: company 0x012d, type 0x0003, two-byte protocol version, two model
 * bytes, then the tag records. Furble reads tag 0x22 at offset 8 and its mode
 * byte at offset 9.
 */
std::vector<uint8_t> advertisement(uint8_t mode22,
                                   uint16_t companyID = 0x012d,
                                   uint16_t type = 0x0003) {
  return {
      static_cast<uint8_t>(companyID & 0xff),
      static_cast<uint8_t>((companyID >> 8) & 0xff),
      static_cast<uint8_t>(type & 0xff),
      static_cast<uint8_t>((type >> 8) & 0xff),
      0x64,
      0x00,
      'U',
      '0',
      0x22,
      mode22,
      0x00,
      0x21,
      0x40,
      0x00,
  };
}

struct Case {
  const char *label;
  uint8_t mode22;
  bool matches;
  Furble::AdvertisementProtocol::SonyGeneration generation;
};

}  // namespace

int main() {
  using Furble::AdvertisementProtocol::SonyGeneration;

  bool passed = true;

  // 0xE8 = bits 7, 6, 5. 0xEC = bits 7, 6, 5, 2. Both clear bit 1 and are the
  // values reported for the 2019 generation.
  const Case cases[] = {
      {"0xE8 is a legacy body",            0xe8, true,  SonyGeneration::Legacy },
      {"0xEC is a legacy body",            0xec, true,  SonyGeneration::Legacy },
      {"0xC8 is a legacy body",            0xc8, true,  SonyGeneration::Legacy },
      {"0xEE is a current body",           0xee, true,  SonyGeneration::Current},
      {"0xE2 is a current body",           0xe2, true,  SonyGeneration::Current},
      {"bit 7 clear is rejected",          0x68, false, SonyGeneration::Legacy },
      {"bit 6 clear is rejected",          0xa8, false, SonyGeneration::Legacy },
      {"both high bits clear is rejected", 0x02, false, SonyGeneration::Legacy },
      {"no bits is rejected",              0x00, false, SonyGeneration::Legacy },
  };

  for (const Case &item : cases) {
    const auto data = advertisement(item.mode22);
    SonyGeneration generation = SonyGeneration::Current;
    const bool matched = Furble::AdvertisementProtocol::matchesSonyAdvertisement(
        data.data(), data.size(), generation);
    passed &= check(matched == item.matches, item.label);
    if (matched) {
      passed &=
          check(generation == item.generation, "the advertisement reports the expected generation");
    }
  }

  // The two-argument overload keeps working for callers that only gate
  // discovery, and must agree with the classifying one.
  const auto legacy = advertisement(0xe8);
  passed &=
      check(Furble::AdvertisementProtocol::matchesSonyAdvertisement(legacy.data(), legacy.size()),
            "the discovery-only overload accepts a legacy body");

  // A nil generation must not be reported when the advertisement is rejected.
  SonyGeneration untouched = SonyGeneration::Current;
  const auto current = advertisement(0xee);
  passed &= check(Furble::AdvertisementProtocol::matchesSonyAdvertisement(current.data(),
                                                                          current.size(), untouched)
                      && (untouched == SonyGeneration::Current),
                  "an accepted current body reports the current generation");

  // Identity and length gates are unchanged.
  const auto otherCompany = advertisement(0xe8, 0x0399);
  passed &= check(!Furble::AdvertisementProtocol::matchesSonyAdvertisement(otherCompany.data(),
                                                                           otherCompany.size()),
                  "a non-Sony company id is rejected");
  const auto otherType = advertisement(0xe8, 0x012d, 0x0004);
  passed &= check(
      !Furble::AdvertisementProtocol::matchesSonyAdvertisement(otherType.data(), otherType.size()),
      "a non-camera device type is rejected");
  passed &= check(!Furble::AdvertisementProtocol::matchesSonyAdvertisement(nullptr, 13),
                  "a null advertisement is rejected");
  const std::vector<uint8_t> shortData = {0x2d, 0x01, 0x03, 0x00, 0x64, 0x00,
                                          'U',  '0',  0x22, 0xe8, 0x00, 0x21};
  passed &= check(
      !Furble::AdvertisementProtocol::matchesSonyAdvertisement(shortData.data(), shortData.size()),
      "a short advertisement is rejected");

  return passed ? 0 : 1;
}
