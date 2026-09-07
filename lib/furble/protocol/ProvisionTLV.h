#ifndef FURBLE_PROVISION_TLV_H
#define FURBLE_PROVISION_TLV_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Furble {
namespace ProvisionTLV {

// A batch is a sequence of:
//
//   uint8 tag       // low seven bits are the field tag; bit 7 means required
//   uint8 type      // one of ValueType below
//   uint8 length
//   uint8 value[length]
//
// Settings use the SETTING tag. Their value starts with the stable companion
// Settings::setting_t::wire_id, followed by the setting value. This keeps the
// batch format compatible with the id/type/len/value settings records in
// plans/50-companion-app-design.md while allowing provisioning-only fields to
// coexist in the same blob.
constexpr uint8_t REQUIRED_TAG_BIT = 0x80;
constexpr uint8_t TAG_MASK = 0x7f;
constexpr size_t RECORD_HEADER_BYTES = 3;
constexpr size_t MAX_BLOB_BYTES = 4096;
constexpr size_t MAX_RECORDS = 64;
constexpr size_t MAX_STRING_BYTES = 63;
constexpr size_t MAX_WIFI_SSID_BYTES = 32;
constexpr size_t MAX_WIFI_PSK_BYTES = 63;
constexpr size_t MAX_COMPANION_PASSWORD_BYTES = 63;
constexpr size_t MAX_MQTT_FIELD_BYTES = 255;
constexpr uint8_t COMPANION_PASSWORD_WIRE_ID = 47;

enum class FieldTag : uint8_t {
  WIFI_SSID = 0x01,
  WIFI_PSK = 0x02,
  SETTING = 0x03,
  COMPANION_PASSWORD = 0x04,
  MQTT_URI = 0x05,
  MQTT_USERNAME = 0x06,
  MQTT_PASSWORD = 0x07,
  MQTT_BASE_TOPIC = 0x08,
};

// These values intentionally match CompanionService's setting_type_t:
// bool=0, u8=1, u32=2, string=3, blob=4.
enum class ValueType : uint8_t {
  BOOL = 0,
  U8 = 1,
  U32 = 2,
  STRING = 3,
  BLOB = 4,
};

enum class ErrorCode : uint8_t {
  NONE,
  NULL_INPUT,
  TRUNCATED,
  MALFORMED,
  UNKNOWN_REQUIRED_FIELD,
  UNKNOWN_SETTING_ID,
  DUPLICATE_FIELD,
  BAD_TYPE,
  BAD_LENGTH,
  OVER_LENGTH,
  INVALID_TEXT,
};

struct Error {
  ErrorCode code = ErrorCode::NONE;
  size_t offset = 0;
};

struct SettingValue {
  uint8_t wireId = 0;
  ValueType type = ValueType::BLOB;
  std::vector<uint8_t> value;

  bool operator==(const SettingValue &other) const;
};

using ByteString = std::vector<uint8_t>;

/** Parsed, typed provisioning fields. An unset optional has no wire record. */
struct ProvisionBundle {
  std::optional<ByteString> wifiSsid;
  std::optional<ByteString> wifiPsk;
  std::vector<SettingValue> settings;
  std::optional<ByteString> companionPassword;
  std::optional<ByteString> mqttUri;
  std::optional<ByteString> mqttUsername;
  std::optional<ByteString> mqttPassword;
  std::optional<ByteString> mqttBaseTopic;

  bool operator==(const ProvisionBundle &other) const;
};

struct SettingSchema {
  uint8_t wireId;
  ValueType type;
  size_t minLength;
  size_t maxLength;
};

/** Return the stable wire schema, or nullptr for an unknown setting id. */
const SettingSchema *schemaForSetting(uint8_t wireId);

/** Human-readable text for a parser error. */
const char *errorString(ErrorCode code);

/** Decode a complete batch. The output is unchanged when decoding fails. */
bool decode(const uint8_t *data, size_t length, ProvisionBundle &out, Error *error = nullptr);

/** Encode a validated bundle into the batch wire format. */
bool encode(const ProvisionBundle &bundle, std::vector<uint8_t> &out, Error *error = nullptr);

enum class TextEncoding : uint8_t {
  HEX,
  BASE64,
};

/**
 * Decode the console text form. An explicit "hex:" or "base64:" prefix may
 * be used; otherwise an even all-hex string is treated as hex and all other
 * input is treated as unpadded or padded standard base64.
 */
bool decodeText(const char *text,
                std::vector<uint8_t> &out,
                TextEncoding *encoding = nullptr,
                Error *error = nullptr);

}  // namespace ProvisionTLV
}  // namespace Furble

#endif
