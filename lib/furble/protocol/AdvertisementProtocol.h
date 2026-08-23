#ifndef FURBLE_ADVERTISEMENT_PROTOCOL_H
#define FURBLE_ADVERTISEMENT_PROTOCOL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Furble {
namespace AdvertisementProtocol {

constexpr uint16_t NIKON_COMPANY_ID = 0x0399;
constexpr uint16_t PANASONIC_COMPANY_ID = 0x003a;
constexpr uint16_t SONY_COMPANY_ID = 0x012d;
constexpr uint16_t SONY_CAMERA_TYPE = 0x0003;

struct NikonAdvertisement {
  uint16_t companyID;
  uint32_t device;
  uint8_t zero;
};

struct SonyAdvertisement {
  uint16_t companyID;
  uint16_t type;
  uint8_t protocolVersion;
  uint8_t unused;
  uint16_t model;
  uint8_t tag22;
  uint8_t mode22;
  uint8_t zero0;
  uint8_t tag21;
  uint8_t mode21;
};

struct LumixAdvertisement {
  uint16_t companyID;
  uint8_t flags;
  std::array<uint8_t, 5> address;
};

bool matchesNikonDiscovery(bool hasManufacturerData, bool advertisesService);
bool parseNikonAdvertisement(const uint8_t *data, size_t bytes, NikonAdvertisement &advertisement);
bool matchesNikonReconnect(const uint8_t *data, size_t bytes, uint32_t device);

bool parseSonyAdvertisement(const uint8_t *data, size_t bytes, SonyAdvertisement &advertisement);
bool matchesSonyAdvertisement(const uint8_t *data, size_t bytes);

bool parseLumixAdvertisement(const uint8_t *data, size_t bytes, LumixAdvertisement &advertisement);
bool matchesLumixAdvertisement(const uint8_t *data, size_t bytes, bool advertisesService);

bool matchesDJIAdvertisement(const uint8_t *data, size_t bytes);
bool matchesRicohName(std::string_view name);

}  // namespace AdvertisementProtocol
}  // namespace Furble

#endif
