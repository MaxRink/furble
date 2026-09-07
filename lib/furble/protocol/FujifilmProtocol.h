#ifndef FURBLE_FUJIFILM_PROTOCOL_H
#define FURBLE_FUJIFILM_PROTOCOL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

/**
 * Render the five advertised serial bytes for display.
 *
 * These are the five serial bytes the Secure advertisement carries. Every byte
 * in the X100VI bench capture is a printable ASCII alphanumeric
 * (31 43 34 46 39, so "1C4F9"), so text is the readable form. A byte outside
 * that set means the field is not text on that body, so fall back to the
 * uppercase hex the scan log has always printed. An all-zero field means
 * nothing was parsed and yields an empty string.
 *
 * Whether these five bytes are the serial the camera itself prints is not
 * established. Body plates carry eight characters, and plan 61 records libfuji
 * reading a full serial from the Device Information Service (0x180a). The
 * bench gate for this change compares the rendered "1C4F9" against the X100VI
 * body plate and against DIS 0x2A25.
 */
std::string formatSerial(const std::array<uint8_t, SERIAL_BYTES> &serial);

/**
 * Compose the most informative name furble can derive for a Secure body.
 *
 * Fujifilm advertises the bare model in the BLE local name ("X100VI"). The
 * longer label on the camera's own Bluetooth screen never reaches the air, so
 * the advertised serial is the only extra identity available before a connect.
 * Appending it separates two bodies of the same model, which the bare model
 * name cannot.
 *
 * Idempotent: a name that already ends in the serial is returned unchanged, so
 * a saved entry rebuilt from NVS never grows a second suffix.
 */
std::string deviceName(const std::string &advertisedName,
                       const std::array<uint8_t, SERIAL_BYTES> &serial);

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
bool isRegistrationNotification(const uint8_t *data, size_t bytes);
bool isGeotagRequest(const uint8_t *data, size_t bytes);

ShutterFrame makeShutterFrame(ShutterAction action);
std::array<uint8_t, GEOTAG_BYTES> encodeGeotag(const GeotagInput &input);

}  // namespace FujifilmProtocol
}  // namespace Furble

#endif
