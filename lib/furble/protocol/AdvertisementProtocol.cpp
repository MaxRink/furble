#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "AdvertisementProtocol.h"

namespace Furble {
namespace AdvertisementProtocol {
namespace {

constexpr size_t NIKON_ADVERTISEMENT_BYTES = 7;
constexpr size_t SONY_ADVERTISEMENT_BYTES = 13;
constexpr size_t LUMIX_ADVERTISEMENT_BYTES = 8;

uint16_t readLittleEndian16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLittleEndian32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

bool matchesNikonDiscovery(bool hasManufacturerData, bool advertisesService) {
  return !hasManufacturerData && advertisesService;
}

bool parseNikonAdvertisement(const uint8_t *data, size_t bytes, NikonAdvertisement &advertisement) {
  if (data == nullptr || bytes < NIKON_ADVERTISEMENT_BYTES) {
    return false;
  }

  advertisement.companyID = readLittleEndian16(data);
  advertisement.device = readLittleEndian32(data + 2);
  advertisement.zero = data[6];
  return true;
}

bool matchesNikonReconnect(const uint8_t *data, size_t bytes, uint32_t device) {
  NikonAdvertisement advertisement = {};
  return parseNikonAdvertisement(data, bytes, advertisement)
         && advertisement.companyID == NIKON_COMPANY_ID && advertisement.device == device;
}

bool parseSonyAdvertisement(const uint8_t *data, size_t bytes, SonyAdvertisement &advertisement) {
  if (data == nullptr || bytes < SONY_ADVERTISEMENT_BYTES) {
    return false;
  }

  advertisement.companyID = readLittleEndian16(data);
  advertisement.type = readLittleEndian16(data + 2);
  advertisement.protocolVersion = data[4];
  advertisement.unused = data[5];
  advertisement.model = readLittleEndian16(data + 6);
  advertisement.tag22 = data[8];
  advertisement.mode22 = data[9];
  advertisement.zero0 = data[10];
  advertisement.tag21 = data[11];
  advertisement.mode21 = data[12];
  return true;
}

bool matchesSonyAdvertisement(const uint8_t *data, size_t bytes) {
  SonyAdvertisement advertisement = {};
  if (!parseSonyAdvertisement(data, bytes, advertisement)
      || advertisement.companyID != SONY_COMPANY_ID || advertisement.type != SONY_CAMERA_TYPE) {
    return false;
  }

  constexpr uint8_t requiredMode = (1U << 7) | (1U << 6) | (1U << 1);
  return (advertisement.mode22 & requiredMode) == requiredMode;
}

bool parseLumixAdvertisement(const uint8_t *data, size_t bytes, LumixAdvertisement &advertisement) {
  if (data == nullptr || bytes < LUMIX_ADVERTISEMENT_BYTES) {
    return false;
  }

  advertisement.companyID = readLittleEndian16(data);
  advertisement.flags = data[2];
  std::copy_n(data + 3, advertisement.address.size(), advertisement.address.begin());
  return true;
}

bool matchesLumixAdvertisement(const uint8_t *data, size_t bytes, bool advertisesService) {
  LumixAdvertisement advertisement = {};
  return advertisesService && parseLumixAdvertisement(data, bytes, advertisement)
         && advertisement.companyID == PANASONIC_COMPANY_ID;
}

bool matchesDJIAdvertisement(const uint8_t *data, size_t bytes) {
  return data != nullptr && bytes >= 5 && data[0] == 0xaa && data[1] == 0x08 && data[4] == 0xfa;
}

bool matchesRicohName(std::string_view name) {
  if (name.empty()) {
    return false;
  }

  std::string upper(name);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  const std::string_view normalized(upper);

  if (normalized == "GR") {
    return true;
  }
  constexpr std::array<std::string_view, 5> prefixes = {"RICOH", "PENTAX", "GR ", "GRIII",
                                                        "GR III"};
  return std::any_of(prefixes.begin(), prefixes.end(), [normalized](std::string_view prefix) {
    return normalized.find(prefix) != std::string_view::npos;
  });
}

}  // namespace AdvertisementProtocol
}  // namespace Furble
