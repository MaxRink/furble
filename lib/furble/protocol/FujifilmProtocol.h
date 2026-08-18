#ifndef FURBLE_FUJIFILM_PROTOCOL_H
#define FURBLE_FUJIFILM_PROTOCOL_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace Furble {
namespace FujifilmProtocol {

constexpr uint16_t COMPANY_ID = 0x04d8;
constexpr uint8_t TOKEN_TYPE = 0x02;
constexpr size_t MANUFACTURER_HEADER_BYTES = 3;
constexpr size_t TOKEN_BYTES = 4;
constexpr size_t SERIAL_BYTES = 5;
constexpr size_t BASIC_ADVERTISEMENT_BYTES = MANUFACTURER_HEADER_BYTES + TOKEN_BYTES;
constexpr size_t SECURE_ADVERTISEMENT_BYTES = MANUFACTURER_HEADER_BYTES + SERIAL_BYTES;
constexpr size_t GEOTAG_BYTES = 23;

struct BasicAdvertisement {
  std::array<uint8_t, TOKEN_BYTES> token;
};

struct SecureAdvertisement {
  std::array<uint8_t, SERIAL_BYTES> serial;
};

enum class ShutterAction : uint8_t {
  PRESS,
  RELEASE,
  FOCUS,
};

struct ShutterFrame {
  std::array<uint8_t, 2> command;
  std::array<uint8_t, 2> parameter;
};

struct GeotagInput {
  double latitude;
  double longitude;
  double altitude;
  unsigned int year;
  unsigned int month;
  unsigned int day;
  unsigned int hour;
  unsigned int minute;
  unsigned int second;
};

bool isFujifilmAdvertisement(const uint8_t *data, size_t bytes);
bool parseBasicAdvertisement(const uint8_t *data, size_t bytes, BasicAdvertisement &advertisement);
bool parseSecureAdvertisement(const uint8_t *data,
                              size_t bytes,
                              SecureAdvertisement &advertisement);
bool matchesBasicAdvertisement(const uint8_t *data,
                               size_t bytes,
                               bool advertisesCrService,
                               bool advertisesXappService);
bool matchesSecureAdvertisement(const uint8_t *data, size_t bytes, bool advertisesService);

bool isConfigurationNotification(const uint8_t *data, size_t bytes);
bool isGeotagRequest(const uint8_t *data, size_t bytes);

ShutterFrame makeShutterFrame(ShutterAction action);
std::array<uint8_t, GEOTAG_BYTES> encodeGeotag(const GeotagInput &input);

}  // namespace FujifilmProtocol
}  // namespace Furble

#endif
