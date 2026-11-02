#include <algorithm>

#include "FujifilmProtocol.h"

#include <algorithm>

namespace Furble {
namespace FujifilmProtocol {
namespace {

template <size_t N>
void writeLittleEndian16(std::array<uint8_t, N> &data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

template <size_t N>
void writeLittleEndian32(std::array<uint8_t, N> &data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
  data[offset + 2] = static_cast<uint8_t>(value >> 16);
  data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

bool isSerialCharacter(uint8_t byte) {
  return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z')
         || (byte >= 'a' && byte <= 'z');
}

}  // namespace

std::string formatSerial(const std::array<uint8_t, SERIAL_BYTES> &serial) {
  bool text = true;
  bool blank = true;
  for (const uint8_t byte : serial) {
    if (byte != 0x00) {
      blank = false;
    }
    if (!isSerialCharacter(byte)) {
      text = false;
    }
  }

  if (blank) {
    return std::string();
  }

  if (text) {
    return std::string(serial.begin(), serial.end());
  }

  static const char digits[] = "0123456789ABCDEF";
  std::string formatted;
  formatted.reserve(SERIAL_BYTES * 2);
  for (const uint8_t byte : serial) {
    formatted.push_back(digits[byte >> 4]);
    formatted.push_back(digits[byte & 0x0f]);
  }
  return formatted;
}

std::string deviceName(const std::string &advertisedName,
                       const std::array<uint8_t, SERIAL_BYTES> &serial) {
  const std::string suffix = formatSerial(serial);
  if (suffix.empty()) {
    return advertisedName;
  }
  if (advertisedName.empty()) {
    return suffix;
  }
  if (advertisedName.size() >= suffix.size()
      && advertisedName.compare(advertisedName.size() - suffix.size(), suffix.size(), suffix)
             == 0) {
    return advertisedName;
  }

  return advertisedName + " " + suffix;
}

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
  return data != nullptr && bytes >= 2 && data[0] == 0x02 && data[1] == 0x00;
}

bool isRegistrationNotification(const uint8_t *data, size_t bytes) {
  // X100VI's CHR_NOT1 capture is 01 00. Older Basic bodies used 02 00 for
  // the same event. The caller must still require the dedicated CHR_NOT1
  // characteristic, since 01 00 is also the geotag request. The official apps
  // additionally treat 01 00 on CHR_NOT1 as a date or time request, so this
  // value is not a proven registration flag; it only means the camera accepted
  // the client enough to talk to it.
  return data != nullptr && bytes >= 2 && data[1] == 0x00 && (data[0] == 0x01 || data[0] == 0x02);
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

std::array<uint8_t, TIME_BYTES> encodeTime(const TimeInput &input) {
  std::array<uint8_t, TIME_BYTES> data = {0x00};
  writeLittleEndian16(data, 0, static_cast<uint16_t>(input.year));
  data[2] = static_cast<uint8_t>(input.month);
  data[3] = static_cast<uint8_t>(input.day);
  data[4] = static_cast<uint8_t>(input.hour);
  data[5] = static_cast<uint8_t>(input.minute);
  data[6] = static_cast<uint8_t>(input.second);
  // The packet stores hundredths of an hour, not minutes: UTC+01:30 is 150,
  // while 90 would encode 0.90 hours. Convert and clamp to the documented
  // -2400..2400 range.
  int64_t hundredths = (static_cast<int64_t>(input.utc_offset_minutes) * 100) / 60;
  hundredths = std::max<int64_t>(-2400, std::min<int64_t>(2400, hundredths));
  // Write the four offset bytes directly. GCC 14's array-bounds analysis
  // conflates the 12-byte time array with the 23-byte geotag array when the
  // shared little-endian helper is inlined for both sizes, and -Werror turns
  // that false positive into a firmware build failure.
  const uint32_t offset = static_cast<uint32_t>(static_cast<int32_t>(hundredths));
  data[7] = static_cast<uint8_t>(offset);
  data[8] = static_cast<uint8_t>(offset >> 8);
  data[9] = static_cast<uint8_t>(offset >> 16);
  data[10] = static_cast<uint8_t>(offset >> 24);
  data[11] = input.dst;
  return data;
}

}  // namespace FujifilmProtocol
}  // namespace Furble
