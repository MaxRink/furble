#include <algorithm>

#include "FujifilmProtocol.h"

namespace Furble {
namespace FujifilmProtocol {
namespace {

void writeLittleEndian16(std::array<uint8_t, GEOTAG_BYTES> &data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeLittleEndian32(std::array<uint8_t, GEOTAG_BYTES> &data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
  data[offset + 2] = static_cast<uint8_t>(value >> 16);
  data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

bool isFujifilmAdvertisement(const uint8_t *data, size_t bytes) {
  if (data == nullptr || bytes < MANUFACTURER_HEADER_BYTES) {
    return false;
  }

  return data[0] == static_cast<uint8_t>(COMPANY_ID)
         && data[1] == static_cast<uint8_t>(COMPANY_ID >> 8);
}

bool parseBasicAdvertisement(const uint8_t *data, size_t bytes, BasicAdvertisement &advertisement) {
  if (!isFujifilmAdvertisement(data, bytes) || bytes != BASIC_ADVERTISEMENT_BYTES
      || data[2] != TOKEN_TYPE) {
    return false;
  }

  std::copy(data + MANUFACTURER_HEADER_BYTES, data + BASIC_ADVERTISEMENT_BYTES,
            advertisement.token.begin());
  return true;
}

bool parseSecureAdvertisement(const uint8_t *data,
                              size_t bytes,
                              SecureAdvertisement &advertisement) {
  if (!isFujifilmAdvertisement(data, bytes) || bytes != SECURE_ADVERTISEMENT_BYTES) {
    return false;
  }

  std::copy(data + MANUFACTURER_HEADER_BYTES, data + SECURE_ADVERTISEMENT_BYTES,
            advertisement.serial.begin());
  return true;
}

bool matchesBasicAdvertisement(const uint8_t *data,
                               size_t bytes,
                               bool advertisesCrService,
                               bool advertisesXappService) {
  BasicAdvertisement advertisement;
  return parseBasicAdvertisement(data, bytes, advertisement)
         && (advertisesCrService || advertisesXappService);
}

bool matchesSecureAdvertisement(const uint8_t *data, size_t bytes, bool advertisesService) {
  SecureAdvertisement advertisement;
  return parseSecureAdvertisement(data, bytes, advertisement) && advertisesService;
}

bool isConfigurationNotification(const uint8_t *data, size_t bytes) {
  // X100VI's CHR_NOT1 capture is 01 00. Older Basic bodies used 02 00 for
  // the same registration-accepted event. The characteristic UUID identifies
  // the event, so this helper accepts both payload variants.
  return data != nullptr && bytes >= 2 && data[1] == 0x00
         && (data[0] == 0x01 || data[0] == 0x02);
}

bool isGeotagRequest(const uint8_t *data, size_t bytes) {
  return data != nullptr && bytes >= 2 && data[0] == 0x01 && data[1] == 0x00;
}

ShutterFrame makeShutterFrame(ShutterAction action) {
  std::array<uint8_t, 2> parameter = {0x00, 0x00};
  switch (action) {
    case ShutterAction::PRESS:
      parameter = {0x02, 0x00};
      break;
    case ShutterAction::RELEASE:
      break;
    case ShutterAction::FOCUS:
      parameter = {0x03, 0x00};
      break;
  }

  return ShutterFrame {
      {0x01, 0x00},
      parameter
  };
}

std::array<uint8_t, GEOTAG_BYTES> encodeGeotag(const GeotagInput &input) {
  std::array<uint8_t, GEOTAG_BYTES> data = {0x00};
  const auto latitude = static_cast<int32_t>(input.latitude * 10000000);
  const auto longitude = static_cast<int32_t>(input.longitude * 10000000);
  const auto altitude = static_cast<int32_t>(input.altitude);

  writeLittleEndian32(data, 0, static_cast<uint32_t>(latitude));
  writeLittleEndian32(data, 4, static_cast<uint32_t>(longitude));
  writeLittleEndian32(data, 8, static_cast<uint32_t>(altitude));
  writeLittleEndian16(data, 16, static_cast<uint16_t>(input.year));
  data[18] = static_cast<uint8_t>(input.month);
  data[19] = static_cast<uint8_t>(input.day);
  data[20] = static_cast<uint8_t>(input.hour);
  data[21] = static_cast<uint8_t>(input.minute);
  data[22] = static_cast<uint8_t>(input.second);

  return data;
}

}  // namespace FujifilmProtocol
}  // namespace Furble
