// Unit tests for the pure Fujifilm wire helpers in
// lib/furble/protocol/FujifilmProtocol.cpp. The camera harness exercises the
// happy path through a live virtual camera, but the parse guards, the service
// flag matching, and the geotag byte packing had no direct negative or
// boundary coverage. These tests call the helpers straight, so a regression in
// a rejection path or a byte offset is caught without a full connect.

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "protocol/FujifilmProtocol.h"

using namespace Furble::FujifilmProtocol;

namespace {

int g_failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_failures++;
  }
}

// The company id is 0x04d8, stored little endian at the front of the
// manufacturer data. A valid basic advertisement is company, token type, then
// a four byte token.
std::vector<uint8_t> basicAdvertisement() {
  return {0xd8, 0x04, TOKEN_TYPE, 0xaa, 0xbb, 0xcc, 0xdd};
}

std::vector<uint8_t> secureAdvertisement() {
  return {0xd8, 0x04, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
}

int32_t readLittleEndian32(const std::array<uint8_t, GEOTAG_BYTES> &data, size_t offset) {
  return static_cast<int32_t>(static_cast<uint32_t>(data[offset])
                              | (static_cast<uint32_t>(data[offset + 1]) << 8)
                              | (static_cast<uint32_t>(data[offset + 2]) << 16)
                              | (static_cast<uint32_t>(data[offset + 3]) << 24));
}

void testIsAdvertisement() {
  const auto basic = basicAdvertisement();
  check(isFujifilmAdvertisement(basic.data(), basic.size()), "the Fujifilm company id is accepted");
  check(!isFujifilmAdvertisement(nullptr, basic.size()), "a null pointer is rejected");

  const std::vector<uint8_t> tooShort = {0xd8, 0x04};
  check(!isFujifilmAdvertisement(tooShort.data(), tooShort.size()),
        "data shorter than the header is rejected");

  const std::vector<uint8_t> wrongCompany = {0xff, 0x04, TOKEN_TYPE, 0, 0, 0, 0};
  check(!isFujifilmAdvertisement(wrongCompany.data(), wrongCompany.size()),
        "a mismatched company low byte is rejected");
  const std::vector<uint8_t> wrongCompanyHigh = {0xd8, 0xff, TOKEN_TYPE, 0, 0, 0, 0};
  check(!isFujifilmAdvertisement(wrongCompanyHigh.data(), wrongCompanyHigh.size()),
        "a mismatched company high byte is rejected");
}

void testParseBasic() {
  const auto advertisement = basicAdvertisement();
  BasicAdvertisement parsed;
  check(parseBasicAdvertisement(advertisement.data(), advertisement.size(), parsed),
        "a well formed basic advertisement parses");
  check(parsed.token == (std::array<uint8_t, TOKEN_BYTES> {0xaa, 0xbb, 0xcc, 0xdd}),
        "the four token bytes are extracted after the header");

  // A basic advertisement must carry the token type in the third byte.
  auto wrongType = basicAdvertisement();
  wrongType[2] = 0x03;
  check(!parseBasicAdvertisement(wrongType.data(), wrongType.size(), parsed),
        "a basic advertisement with the wrong type is rejected");

  // The length must be exactly the basic advertisement size.
  auto tooLong = basicAdvertisement();
  tooLong.push_back(0x00);
  check(!parseBasicAdvertisement(tooLong.data(), tooLong.size(), parsed),
        "a basic advertisement of the wrong length is rejected");
}

void testParseSecure() {
  const auto advertisement = secureAdvertisement();
  SecureAdvertisement parsed;
  check(parseSecureAdvertisement(advertisement.data(), advertisement.size(), parsed),
        "a well formed secure advertisement parses");
  check(parsed.serial == (std::array<uint8_t, SERIAL_BYTES> {0x11, 0x22, 0x33, 0x44, 0x55}),
        "the five serial bytes are extracted after the header");

  // A basic advertisement is one byte short of a secure one and must not parse
  // as secure.
  const auto basic = basicAdvertisement();
  check(!parseSecureAdvertisement(basic.data(), basic.size(), parsed),
        "a basic length advertisement is not a secure advertisement");
}

void testMatches() {
  const auto basic = basicAdvertisement();
  check(matchesBasicAdvertisement(basic.data(), basic.size(), true, false),
        "a basic advertisement with the cr service matches");
  check(matchesBasicAdvertisement(basic.data(), basic.size(), false, true),
        "a basic advertisement with the xapp service matches");
  check(!matchesBasicAdvertisement(basic.data(), basic.size(), false, false),
        "a basic advertisement without a required service does not match");

  auto notBasic = basicAdvertisement();
  notBasic[2] = 0x03;
  check(!matchesBasicAdvertisement(notBasic.data(), notBasic.size(), true, true),
        "a malformed basic advertisement never matches even with services");

  const auto secure = secureAdvertisement();
  check(matchesSecureAdvertisement(secure.data(), secure.size(), true),
        "a secure advertisement with its service matches");
  check(!matchesSecureAdvertisement(secure.data(), secure.size(), false),
        "a secure advertisement without its service does not match");
}

void testNotifications() {
  const std::vector<uint8_t> config = {0x02, 0x00};
  const std::vector<uint8_t> x100vi = {0x01, 0x00};
  const std::vector<uint8_t> geotag = {0x01, 0x00};
  check(isConfigurationNotification(config.data(), config.size()),
        "the legacy configuration notification is recognised");
  check(!isConfigurationNotification(x100vi.data(), x100vi.size()),
        "the X100VI registration is not the legacy configuration header");
  check(isRegistrationNotification(x100vi.data(), x100vi.size()),
        "the captured X100VI 01 00 notification is recognised");
  check(isRegistrationNotification(config.data(), config.size()),
        "the legacy Basic registration payload is recognised");
  check(isGeotagRequest(geotag.data(), geotag.size()), "the geotag request header is recognised");
  check(!isGeotagRequest(config.data(), config.size()),
        "a configuration header is not a geotag request");

  const std::vector<uint8_t> tooShort = {0x02};
  check(!isConfigurationNotification(tooShort.data(), tooShort.size()),
        "a single byte notification is rejected");
  const std::vector<uint8_t> wrongValue = {0x01, 0x01};
  check(!isRegistrationNotification(wrongValue.data(), wrongValue.size()),
        "a malformed registration payload is rejected");
  check(!isRegistrationNotification(nullptr, 2), "a null registration notification is rejected");
  check(!isGeotagRequest(nullptr, 2), "a null notification pointer is rejected");
}

void testShutterFrames() {
  const auto press = makeShutterFrame(ShutterAction::PRESS);
  check((press.command == std::array<uint8_t, 2> {0x01, 0x00}), "the shutter command is fixed");
  check((press.parameter == std::array<uint8_t, 2> {0x02, 0x00}), "press uses the press parameter");

  const auto release = makeShutterFrame(ShutterAction::RELEASE);
  check((release.parameter == std::array<uint8_t, 2> {0x00, 0x00}),
        "release clears the parameter bytes");

  const auto focus = makeShutterFrame(ShutterAction::FOCUS);
  check((focus.parameter == std::array<uint8_t, 2> {0x03, 0x00}), "focus uses the focus parameter");
}

void testGeotag() {
  GeotagInput input = {};
  input.latitude = 1.0;
  input.longitude = -1.0;
  input.altitude = 34.9;
  input.year = 2026;
  input.month = 8;
  input.day = 17;
  input.hour = 12;
  input.minute = 34;
  input.second = 56;

  const auto data = encodeGeotag(input);

  // Latitude and longitude are scaled by ten million and stored little endian.
  check(readLittleEndian32(data, 0) == 10000000, "latitude is scaled by ten million");
  check(readLittleEndian32(data, 4) == -10000000, "negative longitude is two's complement");
  // The known scale also fixes the exact byte pattern for one degree.
  check(data[0] == 0x80 && data[1] == 0x96 && data[2] == 0x98 && data[3] == 0x00,
        "one degree of latitude packs to the expected little endian bytes");

  // Altitude truncates toward zero.
  check(readLittleEndian32(data, 8) == 34, "altitude truncates to whole metres");

  // Bytes 12 through 15 are reserved and stay zero.
  check(data[12] == 0 && data[13] == 0 && data[14] == 0 && data[15] == 0,
        "the reserved geotag bytes stay zero");

  // The date and time fields follow at their fixed offsets.
  check(data[16] == static_cast<uint8_t>(2026) && data[17] == static_cast<uint8_t>(2026 >> 8),
        "the year is little endian at offset sixteen");
  check(data[18] == 8, "the month is at offset eighteen");
  check(data[19] == 17, "the day is at offset nineteen");
  check(data[20] == 12, "the hour is at offset twenty");
  check(data[21] == 34, "the minute is at offset twenty one");
  check(data[22] == 56, "the second is at offset twenty two");
}

}  // namespace

int main() {
  testIsAdvertisement();
  testParseBasic();
  testParseSecure();
  testMatches();
  testNotifications();
  testShutterFrames();
  testGeotag();

  if (g_failures > 0) {
    std::cerr << "fujifilm protocol tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "fujifilm protocol tests: PASS\n";
  return 0;
}
