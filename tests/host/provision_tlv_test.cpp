// Host tests for the dependency-free batch provisioning codec.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "protocol/ProvisionTLV.h"

namespace {

using Furble::ProvisionTLV::ByteString;
using Furble::ProvisionTLV::Error;
using Furble::ProvisionTLV::ErrorCode;
using Furble::ProvisionTLV::FieldTag;
using Furble::ProvisionTLV::ProvisionBundle;
using Furble::ProvisionTLV::SettingValue;
using Furble::ProvisionTLV::ValueType;
using Bytes = std::vector<uint8_t>;

int g_failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_failures++;
  }
}

Bytes text(const char *value) {
  return Bytes(value, value + std::strlen(value));
}

Bytes record(FieldTag tag, ValueType type, const Bytes &value, bool required = true) {
  Bytes bytes = {
      static_cast<uint8_t>(static_cast<uint8_t>(tag)
                           | (required ? Furble::ProvisionTLV::REQUIRED_TAG_BIT : 0)),
      static_cast<uint8_t>(type),
      static_cast<uint8_t>(value.size()),
  };
  bytes.insert(bytes.end(), value.begin(), value.end());
  return bytes;
}

void append(Bytes &target, const Bytes &source) {
  target.insert(target.end(), source.begin(), source.end());
}

void expectRejected(const Bytes &bytes, ErrorCode expected, const std::string &message) {
  ProvisionBundle output;
  output.wifiSsid = text("unchanged");
  const ProvisionBundle before = output;
  Error error;
  check(!Furble::ProvisionTLV::decode(bytes.data(), bytes.size(), output, &error), message);
  check(error.code == expected,
        message + " reports " + Furble::ProvisionTLV::errorString(expected));
  check(output == before, message + " does not partially populate the output");
}

void testRoundTrip() {
  ProvisionBundle original;
  original.wifiSsid = text("studio-network");
  original.wifiPsk = text("correct horse battery staple");
  original.companionPassword = text("companion-secret");
  original.mqttUri = text("mqtt://broker.local:1883");
  original.mqttUsername = text("furble");
  original.mqttPassword = text("mqtt-secret");
  original.mqttBaseTopic = text("studio/camera");
  original.settings = {
      {1, ValueType::U8,     {0x09}                  },
      {3, ValueType::STRING, text("Dark")            },
      {5, ValueType::BOOL,   {0x01}                  },
      {6, ValueType::U32,    {0x78, 0x56, 0x34, 0x12}},
  };

  Bytes encoded;
  Error error;
  check(Furble::ProvisionTLV::encode(original, encoded, &error),
        "encode accepts a valid multi-field bundle");
  check(!encoded.empty(), "a non-empty bundle produces bytes");
  check(encoded[0]
            == (static_cast<uint8_t>(FieldTag::WIFI_SSID) | Furble::ProvisionTLV::REQUIRED_TAG_BIT),
        "encoded records carry the required tag bit");
  check(encoded[1] == static_cast<uint8_t>(ValueType::STRING), "encoded records carry the type");

  ProvisionBundle decoded;
  check(Furble::ProvisionTLV::decode(encoded.data(), encoded.size(), decoded, &error),
        "decode accepts the encoder output");
  check(decoded == original, "encode then decode preserves every field");
}

void testMalformedAndTruncated() {
  expectRejected({0x81}, ErrorCode::TRUNCATED, "truncated TLV header");
  expectRejected({0x81, static_cast<uint8_t>(ValueType::STRING)}, ErrorCode::TRUNCATED,
                 "truncated TLV length");
  expectRejected({0x81, static_cast<uint8_t>(ValueType::STRING), 2, 'a'}, ErrorCode::TRUNCATED,
                 "declared value longer than the blob");

  Bytes unknownOptional = record(static_cast<FieldTag>(0x7e), ValueType::BLOB, {0x01}, false);
  ProvisionBundle optionalOutput;
  Error error;
  check(Furble::ProvisionTLV::decode(unknownOptional.data(), unknownOptional.size(), optionalOutput,
                                     &error),
        "unknown optional fields are forward-compatible");
  check(optionalOutput == ProvisionBundle {}, "unknown optional fields do not enter the bundle");

  const Bytes unknownRequired = record(static_cast<FieldTag>(0x7e), ValueType::BLOB, {0x01});
  expectRejected(unknownRequired, ErrorCode::UNKNOWN_REQUIRED_FIELD,
                 "unknown required field is rejected");
  expectRejected(record(FieldTag::SETTING, ValueType::U8, {0xfe, 0x01}),
                 ErrorCode::UNKNOWN_SETTING_ID, "unknown settings wire id is rejected");
  expectRejected(record(FieldTag::SETTING, ValueType::STRING, {0x01, 'x'}), ErrorCode::BAD_TYPE,
                 "setting with a mismatched type is rejected");
  expectRejected(record(FieldTag::WIFI_SSID, ValueType::STRING, {'a', 0}), ErrorCode::MALFORMED,
                 "string containing NUL is rejected");

  Bytes duplicate = record(FieldTag::WIFI_SSID, ValueType::STRING, {'a'});
  append(duplicate, record(FieldTag::WIFI_SSID, ValueType::STRING, {'b'}));
  expectRejected(duplicate, ErrorCode::DUPLICATE_FIELD, "duplicate field is rejected");

  Bytes overSsid = record(FieldTag::WIFI_SSID, ValueType::STRING, Bytes(33, 's'));
  expectRejected(overSsid, ErrorCode::OVER_LENGTH, "SSID over the 32-byte limit is rejected");

  Bytes overSetting = {
      static_cast<uint8_t>(FieldTag::SETTING) | Furble::ProvisionTLV::REQUIRED_TAG_BIT,
      static_cast<uint8_t>(ValueType::STRING), 65, 3};
  overSetting.insert(overSetting.end(), 64, 't');
  expectRejected(overSetting, ErrorCode::OVER_LENGTH,
                 "64-byte string setting over the 63-byte limit is rejected");

  Bytes tooLarge(Furble::ProvisionTLV::MAX_BLOB_BYTES + 1, 0);
  expectRejected(tooLarge, ErrorCode::OVER_LENGTH, "blob over the global limit is rejected");
  expectRejected({0x81, static_cast<uint8_t>(ValueType::STRING), 0}, ErrorCode::BAD_LENGTH,
                 "empty required SSID is rejected");
  check(!Furble::ProvisionTLV::decode(nullptr, 1, optionalOutput, &error),
        "null input with a nonzero length is rejected");
  check(error.code == ErrorCode::NULL_INPUT, "null input reports the right error");
}

void testEncoderValidation() {
  ProvisionBundle overLength;
  overLength.wifiSsid = ByteString(33, 's');
  Bytes encoded = {0xaa};
  Error error;
  check(!Furble::ProvisionTLV::encode(overLength, encoded, &error),
        "encoder rejects an over-length SSID");
  check(error.code == ErrorCode::OVER_LENGTH, "encoder reports over-length");
  check(encoded.empty(), "encoder clears output after a validation failure");

  ProvisionBundle unknownSetting;
  unknownSetting.settings.push_back({0xfe, ValueType::U8, {1}});
  check(!Furble::ProvisionTLV::encode(unknownSetting, encoded, &error),
        "encoder rejects an unknown setting id");
  check(error.code == ErrorCode::UNKNOWN_SETTING_ID, "encoder reports unknown setting id");

  ProvisionBundle duplicateSettings;
  duplicateSettings.settings = {
      {1, ValueType::U8, {1}},
      {1, ValueType::U8, {2}},
  };
  check(!Furble::ProvisionTLV::encode(duplicateSettings, encoded, &error),
        "encoder rejects duplicate settings");
  check(error.code == ErrorCode::DUPLICATE_FIELD, "encoder reports duplicate setting");
}

void testTextDecoding() {
  Bytes bytes;
  Error error;
  Furble::ProvisionTLV::TextEncoding encoding;
  check(Furble::ProvisionTLV::decodeText("010203", bytes, &encoding, &error),
        "auto-detected hex text decodes");
  check(encoding == Furble::ProvisionTLV::TextEncoding::HEX && bytes == Bytes({1, 2, 3}),
        "hex text has the expected bytes");

  check(Furble::ProvisionTLV::decodeText("base64:AQID", bytes, &encoding, &error),
        "base64 text decodes");
  check(encoding == Furble::ProvisionTLV::TextEncoding::BASE64 && bytes == Bytes({1, 2, 3}),
        "base64 text has the expected bytes");
  check(Furble::ProvisionTLV::decodeText("AQID", bytes, nullptr, &error),
        "unpadded base64 text decodes");
  check(Furble::ProvisionTLV::decodeText("abc", bytes, &encoding, &error),
        "odd-length non-byte text falls back to base64");
  check(encoding == Furble::ProvisionTLV::TextEncoding::BASE64 && bytes == Bytes({0x69, 0xb7}),
        "base64 fallback has the expected bytes");
  check(!Furble::ProvisionTLV::decodeText("base64:A", bytes, nullptr, &error),
        "truncated base64 quantum is rejected");
}

// Every setting the firmware can store has to have a schema row here, or
// provisioning answers UNSUPPORTED_SETTING for it and a batch that carries it
// is rejected whole. A new wire id is easy to add to FurbleSettings.h and easy
// to forget here, so the ids this branch adds are named rather than counted.
void testSettingSchemas(void) {
  const auto *legend = Furble::ProvisionTLV::schemaForSetting(65);
  check(legend != nullptr, "legend placement (65) has a provisioning schema");
  if (legend != nullptr) {
    check(legend->type == ValueType::U8, "legend placement is a single byte");
    check(legend->minLength == 1 && legend->maxLength == 1,
          "legend placement carries exactly one byte");
  }

  // The negative half: an id with no row is refused rather than guessed at, so
  // the assertion above fails if the row is deleted rather than passing by
  // accident on some default.
  check(Furble::ProvisionTLV::schemaForSetting(200) == nullptr,
        "an unknown setting id has no schema");
}

}  // namespace

int main() {
  testRoundTrip();
  testMalformedAndTruncated();
  testEncoderValidation();
  testTextDecoding();
  testSettingSchemas();

  if (g_failures != 0) {
    std::cerr << "provision tlv tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "provision tlv tests: PASS\n";
  return 0;
}
