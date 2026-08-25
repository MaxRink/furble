#include "ProvisionTLV.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace Furble {
namespace ProvisionTLV {

namespace {

// This is the dependency-free mirror of CompanionService::settingType. The
// runtime apply path still asks Settings::getByWireId(), so a setting that is
// conditional on the board cannot accidentally be written when it is absent.
constexpr SettingSchema SETTING_SCHEMAS[] = {
    {1,                          ValueType::U8,     1,  1                           },
    {2,                          ValueType::U8,     1,  1                           },
    {3,                          ValueType::STRING, 0,  MAX_STRING_BYTES            },
    {4,                          ValueType::U8,     1,  1                           },
    {5,                          ValueType::BOOL,   1,  1                           },
    {6,                          ValueType::U32,    4,  4                           },
    {7,                          ValueType::BLOB,   12, 12                          },
    {8,                          ValueType::BOOL,   1,  1                           },
    {9,                          ValueType::BOOL,   1,  1                           },
    {10,                         ValueType::BOOL,   1,  1                           },
    {11,                         ValueType::BOOL,   1,  1                           },
    {12,                         ValueType::BOOL,   1,  1                           },
    {13,                         ValueType::U8,     1,  1                           },
    {14,                         ValueType::BOOL,   1,  1                           },
    {15,                         ValueType::U8,     1,  1                           },
    {16,                         ValueType::BOOL,   1,  1                           },
    {17,                         ValueType::U8,     1,  1                           },
    {18,                         ValueType::U8,     1,  1                           },
    {19,                         ValueType::BOOL,   1,  1                           },
    {20,                         ValueType::BOOL,   1,  1                           },
    {21,                         ValueType::U8,     1,  1                           },
    {22,                         ValueType::U32,    4,  4                           },
    {23,                         ValueType::BOOL,   1,  1                           },
    {24,                         ValueType::U8,     1,  1                           },
    {25,                         ValueType::U8,     1,  1                           },
    {26,                         ValueType::U8,     1,  1                           },
    {27,                         ValueType::STRING, 0,  MAX_STRING_BYTES            },
    {28,                         ValueType::BOOL,   1,  1                           },
    {29,                         ValueType::BOOL,   1,  1                           },
    {30,                         ValueType::BOOL,   1,  1                           },
    {31,                         ValueType::BOOL,   1,  1                           },
    {32,                         ValueType::U8,     1,  1                           },
    {33,                         ValueType::U8,     1,  1                           },
    {34,                         ValueType::U8,     1,  1                           },
    {35,                         ValueType::U8,     1,  1                           },
    {36,                         ValueType::U8,     1,  1                           },
    {37,                         ValueType::U8,     1,  1                           },
    {38,                         ValueType::U8,     1,  1                           },
    {39,                         ValueType::BOOL,   1,  1                           },
    {40,                         ValueType::U8,     1,  1                           },
    {41,                         ValueType::U8,     1,  1                           },
    {44,                         ValueType::BOOL,   1,  1                           },
    {COMPANION_PASSWORD_WIRE_ID, ValueType::STRING, 1,  MAX_COMPANION_PASSWORD_BYTES},
};

struct FieldSchema {
  uint8_t tag;
  ValueType type;
  size_t minLength;
  size_t maxLength;
};

constexpr FieldSchema FIELD_SCHEMAS[] = {
    {static_cast<uint8_t>(FieldTag::WIFI_SSID),          ValueType::STRING, 1, MAX_WIFI_SSID_BYTES },
    {static_cast<uint8_t>(FieldTag::WIFI_PSK),           ValueType::STRING, 0, MAX_WIFI_PSK_BYTES  },
    {static_cast<uint8_t>(FieldTag::COMPANION_PASSWORD), ValueType::STRING, 1,
     MAX_COMPANION_PASSWORD_BYTES                                                                  },
    {static_cast<uint8_t>(FieldTag::MQTT_URI),           ValueType::STRING, 1, MAX_MQTT_FIELD_BYTES},
    {static_cast<uint8_t>(FieldTag::MQTT_USERNAME),      ValueType::STRING, 0, MAX_MQTT_FIELD_BYTES},
    {static_cast<uint8_t>(FieldTag::MQTT_PASSWORD),      ValueType::STRING, 0, MAX_MQTT_FIELD_BYTES},
    {static_cast<uint8_t>(FieldTag::MQTT_BASE_TOPIC),    ValueType::STRING, 1, MAX_MQTT_FIELD_BYTES},
};

void setError(Error *error, ErrorCode code, size_t offset) {
  if (error != nullptr) {
    error->code = code;
    error->offset = offset;
  }
}

bool isValueType(ValueType type) {
  switch (type) {
    case ValueType::BOOL:
    case ValueType::U8:
    case ValueType::U32:
    case ValueType::STRING:
    case ValueType::BLOB:
      return true;
  }
  return false;
}

const FieldSchema *schemaForField(uint8_t tag) {
  for (const auto &schema : FIELD_SCHEMAS) {
    if (schema.tag == tag) {
      return &schema;
    }
  }
  return nullptr;
}

ErrorCode validateValue(ValueType type,
                        const uint8_t *value,
                        size_t length,
                        size_t minLength,
                        size_t maxLength) {
  if (!isValueType(type)) {
    return ErrorCode::BAD_TYPE;
  }
  if (length > maxLength) {
    return ErrorCode::OVER_LENGTH;
  }
  if (length < minLength) {
    return ErrorCode::BAD_LENGTH;
  }
  if ((type == ValueType::STRING) && (length != 0)
      && (std::memchr(value, '\0', length) != nullptr)) {
    return ErrorCode::MALFORMED;
  }
  return ErrorCode::NONE;
}

bool hasSetting(const std::vector<SettingValue> &settings, uint8_t wireId, size_t skip = SIZE_MAX) {
  for (size_t i = 0; i < settings.size(); i++) {
    if ((i != skip) && (settings[i].wireId == wireId)) {
      return true;
    }
  }
  return false;
}

bool appendRecord(std::vector<uint8_t> &bytes,
                  uint8_t tag,
                  ValueType type,
                  const std::vector<uint8_t> &value,
                  Error *error) {
  if (value.size() > UINT8_MAX) {
    setError(error, ErrorCode::OVER_LENGTH, bytes.size());
    return false;
  }
  const size_t recordSize = RECORD_HEADER_BYTES + value.size();
  if ((bytes.size() > MAX_BLOB_BYTES) || (recordSize > (MAX_BLOB_BYTES - bytes.size()))) {
    setError(error, ErrorCode::OVER_LENGTH, bytes.size());
    return false;
  }

  bytes.push_back(static_cast<uint8_t>(tag | REQUIRED_TAG_BIT));
  bytes.push_back(static_cast<uint8_t>(type));
  bytes.push_back(static_cast<uint8_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
  return true;
}

template <typename T>
bool appendOptional(std::vector<uint8_t> &bytes,
                    const std::optional<T> &field,
                    FieldTag tag,
                    ValueType type,
                    size_t minLength,
                    size_t maxLength,
                    Error *error) {
  if (!field.has_value()) {
    return true;
  }

  const auto &value = *field;
  const ErrorCode validation =
      validateValue(type, value.data(), value.size(), minLength, maxLength);
  if (validation != ErrorCode::NONE) {
    setError(error, validation, bytes.size());
    return false;
  }
  return appendRecord(bytes, static_cast<uint8_t>(tag), type, value, error);
}

int hexValue(char value) {
  if ((value >= '0') && (value <= '9')) {
    return value - '0';
  }
  if ((value >= 'a') && (value <= 'f')) {
    return value - 'a' + 10;
  }
  if ((value >= 'A') && (value <= 'F')) {
    return value - 'A' + 10;
  }
  return -1;
}

int base64Value(char value) {
  if ((value >= 'A') && (value <= 'Z')) {
    return value - 'A';
  }
  if ((value >= 'a') && (value <= 'z')) {
    return value - 'a' + 26;
  }
  if ((value >= '0') && (value <= '9')) {
    return value - '0' + 52;
  }
  if (value == '+') {
    return 62;
  }
  if (value == '/') {
    return 63;
  }
  return -1;
}

bool decodeHex(const std::string &text, std::vector<uint8_t> &out, Error *error) {
  if ((text.size() % 2) != 0) {
    setError(error, ErrorCode::INVALID_TEXT, text.size());
    return false;
  }
  out.reserve(text.size() / 2);
  for (size_t i = 0; i < text.size(); i += 2) {
    const int high = hexValue(text[i]);
    const int low = hexValue(text[i + 1]);
    if ((high < 0) || (low < 0)) {
      setError(error, ErrorCode::INVALID_TEXT, i);
      out.clear();
      return false;
    }
    if (out.size() >= MAX_BLOB_BYTES) {
      setError(error, ErrorCode::OVER_LENGTH, i);
      out.clear();
      return false;
    }
    out.push_back(static_cast<uint8_t>((high << 4) | low));
  }
  return true;
}

bool decodeBase64(const std::string &text, std::vector<uint8_t> &out, Error *error) {
  const size_t firstPadding = text.find('=');
  const size_t dataLength = (firstPadding == std::string::npos) ? text.size() : firstPadding;
  const size_t padding = (firstPadding == std::string::npos) ? 0 : text.size() - firstPadding;

  if ((padding > 2) || ((padding != 0) && ((text.size() % 4) != 0)) || ((dataLength % 4) == 1)) {
    setError(error, ErrorCode::INVALID_TEXT, dataLength);
    return false;
  }
  if ((padding == 1) && ((dataLength % 4) != 3)) {
    setError(error, ErrorCode::INVALID_TEXT, firstPadding);
    return false;
  }
  if ((padding == 2) && ((dataLength % 4) != 2)) {
    setError(error, ErrorCode::INVALID_TEXT, firstPadding);
    return false;
  }
  for (size_t i = dataLength; i < text.size(); i++) {
    if (text[i] != '=') {
      setError(error, ErrorCode::INVALID_TEXT, i);
      return false;
    }
  }

  uint32_t accumulator = 0;
  unsigned bits = 0;
  out.reserve((dataLength * 3) / 4 + 1);
  for (size_t i = 0; i < dataLength; i++) {
    const int value = base64Value(text[i]);
    if (value < 0) {
      setError(error, ErrorCode::INVALID_TEXT, i);
      out.clear();
      return false;
    }
    accumulator = ((accumulator << 6) | static_cast<uint32_t>(value)) & 0x00ffffffU;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out.size() >= MAX_BLOB_BYTES) {
        setError(error, ErrorCode::OVER_LENGTH, i);
        out.clear();
        return false;
      }
      out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xffU));
    }
  }

  if ((bits != 0) && ((accumulator & ((1U << bits) - 1U)) != 0)) {
    setError(error, ErrorCode::INVALID_TEXT, dataLength);
    out.clear();
    return false;
  }
  return true;
}

}  // namespace

bool SettingValue::operator==(const SettingValue &other) const {
  return (wireId == other.wireId) && (type == other.type) && (value == other.value);
}

bool ProvisionBundle::operator==(const ProvisionBundle &other) const {
  return (wifiSsid == other.wifiSsid) && (wifiPsk == other.wifiPsk) && (settings == other.settings)
         && (companionPassword == other.companionPassword) && (mqttUri == other.mqttUri)
         && (mqttUsername == other.mqttUsername) && (mqttPassword == other.mqttPassword)
         && (mqttBaseTopic == other.mqttBaseTopic);
}

const SettingSchema *schemaForSetting(uint8_t wireId) {
  for (const auto &schema : SETTING_SCHEMAS) {
    if (schema.wireId == wireId) {
      return &schema;
    }
  }
  return nullptr;
}

const char *errorString(ErrorCode code) {
  switch (code) {
    case ErrorCode::NONE:
      return "ok";
    case ErrorCode::NULL_INPUT:
      return "null input";
    case ErrorCode::TRUNCATED:
      return "truncated record";
    case ErrorCode::MALFORMED:
      return "malformed value";
    case ErrorCode::UNKNOWN_REQUIRED_FIELD:
      return "unknown required field";
    case ErrorCode::UNKNOWN_SETTING_ID:
      return "unknown setting id";
    case ErrorCode::DUPLICATE_FIELD:
      return "duplicate field";
    case ErrorCode::BAD_TYPE:
      return "bad type";
    case ErrorCode::BAD_LENGTH:
      return "bad length";
    case ErrorCode::OVER_LENGTH:
      return "value too long";
    case ErrorCode::INVALID_TEXT:
      return "invalid hex/base64 text";
  }
  return "unknown error";
}

bool decode(const uint8_t *data, size_t length, ProvisionBundle &out, Error *error) {
  if (error != nullptr) {
    *error = {};
  }
  if ((length != 0) && (data == nullptr)) {
    setError(error, ErrorCode::NULL_INPUT, 0);
    return false;
  }
  if (length > MAX_BLOB_BYTES) {
    setError(error, ErrorCode::OVER_LENGTH, 0);
    return false;
  }

  ProvisionBundle decoded;
  size_t offset = 0;
  size_t records = 0;
  while (offset < length) {
    const size_t recordOffset = offset;
    if ((length - offset) < RECORD_HEADER_BYTES) {
      setError(error, ErrorCode::TRUNCATED, recordOffset);
      return false;
    }

    const uint8_t rawTag = data[offset++];
    const uint8_t tag = rawTag & TAG_MASK;
    const bool required = (rawTag & REQUIRED_TAG_BIT) != 0;
    const ValueType type = static_cast<ValueType>(data[offset++]);
    const size_t valueLength = data[offset++];
    if (valueLength > (length - offset)) {
      setError(error, ErrorCode::TRUNCATED, recordOffset);
      return false;
    }
    if (++records > MAX_RECORDS) {
      setError(error, ErrorCode::OVER_LENGTH, recordOffset);
      return false;
    }

    const uint8_t *value = data + offset;
    if (tag == 0) {
      setError(error, ErrorCode::MALFORMED, recordOffset);
      return false;
    }

    if (tag == static_cast<uint8_t>(FieldTag::SETTING)) {
      if (valueLength == 0) {
        setError(error, ErrorCode::BAD_LENGTH, recordOffset);
        return false;
      }
      const uint8_t wireId = value[0];
      const SettingSchema *schema = schemaForSetting(wireId);
      if (schema == nullptr) {
        // A settings write is never silently ignored: a typo in a wire id
        // must not look like a successful provisioning batch.
        setError(error, ErrorCode::UNKNOWN_SETTING_ID, recordOffset);
        return false;
      }
      if (type != schema->type) {
        setError(error, ErrorCode::BAD_TYPE, recordOffset);
        return false;
      }
      const size_t settingLength = valueLength - 1;
      const ErrorCode validation =
          validateValue(type, value + 1, settingLength, schema->minLength, schema->maxLength);
      if (validation != ErrorCode::NONE) {
        setError(error, validation, recordOffset);
        return false;
      }
      if (hasSetting(decoded.settings, wireId)) {
        setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
        return false;
      }
      if ((wireId == COMPANION_PASSWORD_WIRE_ID) && decoded.companionPassword.has_value()) {
        setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
        return false;
      }
      SettingValue setting;
      setting.wireId = wireId;
      setting.type = type;
      setting.value.assign(value + 1, value + valueLength);
      decoded.settings.push_back(std::move(setting));
    } else {
      const FieldSchema *schema = schemaForField(tag);
      if (schema == nullptr) {
        if (required) {
          setError(error, ErrorCode::UNKNOWN_REQUIRED_FIELD, recordOffset);
          return false;
        }
        offset += valueLength;
        continue;
      }
      if (type != schema->type) {
        setError(error, ErrorCode::BAD_TYPE, recordOffset);
        return false;
      }
      const ErrorCode validation =
          validateValue(type, value, valueLength, schema->minLength, schema->maxLength);
      if (validation != ErrorCode::NONE) {
        setError(error, validation, recordOffset);
        return false;
      }

      ByteString field(value, value + valueLength);
      switch (static_cast<FieldTag>(tag)) {
        case FieldTag::WIFI_SSID:
          if (decoded.wifiSsid.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.wifiSsid = std::move(field);
          break;
        case FieldTag::WIFI_PSK:
          if (decoded.wifiPsk.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.wifiPsk = std::move(field);
          break;
        case FieldTag::COMPANION_PASSWORD:
          if (decoded.companionPassword.has_value()
              || hasSetting(decoded.settings, COMPANION_PASSWORD_WIRE_ID)) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.companionPassword = std::move(field);
          break;
        case FieldTag::MQTT_URI:
          if (decoded.mqttUri.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.mqttUri = std::move(field);
          break;
        case FieldTag::MQTT_USERNAME:
          if (decoded.mqttUsername.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.mqttUsername = std::move(field);
          break;
        case FieldTag::MQTT_PASSWORD:
          if (decoded.mqttPassword.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.mqttPassword = std::move(field);
          break;
        case FieldTag::MQTT_BASE_TOPIC:
          if (decoded.mqttBaseTopic.has_value()) {
            setError(error, ErrorCode::DUPLICATE_FIELD, recordOffset);
            return false;
          }
          decoded.mqttBaseTopic = std::move(field);
          break;
        case FieldTag::SETTING:
          // Handled above; keep the switch exhaustive if a new tag is added.
          setError(error, ErrorCode::MALFORMED, recordOffset);
          return false;
      }
    }
    offset += valueLength;
  }

  out = std::move(decoded);
  return true;
}

bool encode(const ProvisionBundle &bundle, std::vector<uint8_t> &out, Error *error) {
  if (error != nullptr) {
    *error = {};
  }
  out.clear();

  auto append = [&](bool ok) {
    if (!ok) {
      out.clear();
    }
    return ok;
  };

  if (!append(appendOptional(out, bundle.wifiSsid, FieldTag::WIFI_SSID, ValueType::STRING, 1,
                             MAX_WIFI_SSID_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.wifiPsk, FieldTag::WIFI_PSK, ValueType::STRING, 0,
                             MAX_WIFI_PSK_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.companionPassword, FieldTag::COMPANION_PASSWORD,
                             ValueType::STRING, 1, MAX_COMPANION_PASSWORD_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.mqttUri, FieldTag::MQTT_URI, ValueType::STRING, 1,
                             MAX_MQTT_FIELD_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.mqttUsername, FieldTag::MQTT_USERNAME, ValueType::STRING,
                             0, MAX_MQTT_FIELD_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.mqttPassword, FieldTag::MQTT_PASSWORD, ValueType::STRING,
                             0, MAX_MQTT_FIELD_BYTES, error))) {
    return false;
  }
  if (!append(appendOptional(out, bundle.mqttBaseTopic, FieldTag::MQTT_BASE_TOPIC,
                             ValueType::STRING, 1, MAX_MQTT_FIELD_BYTES, error))) {
    return false;
  }

  for (size_t i = 0; i < bundle.settings.size(); i++) {
    const auto &setting = bundle.settings[i];
    const SettingSchema *schema = schemaForSetting(setting.wireId);
    if (schema == nullptr) {
      setError(error, ErrorCode::UNKNOWN_SETTING_ID, i);
      out.clear();
      return false;
    }
    if (setting.type != schema->type) {
      setError(error, ErrorCode::BAD_TYPE, i);
      out.clear();
      return false;
    }
    const ErrorCode validation =
        validateValue(setting.type, setting.value.data(), setting.value.size(), schema->minLength,
                      schema->maxLength);
    if (validation != ErrorCode::NONE) {
      setError(error, validation, i);
      out.clear();
      return false;
    }
    if (hasSetting(bundle.settings, setting.wireId, i)) {
      setError(error, ErrorCode::DUPLICATE_FIELD, i);
      out.clear();
      return false;
    }
    if ((setting.wireId == COMPANION_PASSWORD_WIRE_ID) && bundle.companionPassword.has_value()) {
      setError(error, ErrorCode::DUPLICATE_FIELD, i);
      out.clear();
      return false;
    }

    std::vector<uint8_t> payload;
    payload.reserve(setting.value.size() + 1);
    payload.push_back(setting.wireId);
    payload.insert(payload.end(), setting.value.begin(), setting.value.end());
    if (!append(appendRecord(out, static_cast<uint8_t>(FieldTag::SETTING), setting.type, payload,
                             error))) {
      return false;
    }
  }

  return true;
}

bool decodeText(const char *text, std::vector<uint8_t> &out, TextEncoding *encoding, Error *error) {
  if (error != nullptr) {
    *error = {};
  }
  out.clear();
  if (text == nullptr) {
    setError(error, ErrorCode::NULL_INPUT, 0);
    return false;
  }

  std::string input(text);
  TextEncoding selected = TextEncoding::BASE64;
  if (input.compare(0, 4, "hex:") == 0) {
    selected = TextEncoding::HEX;
    input.erase(0, 4);
  } else if (input.compare(0, 7, "base64:") == 0) {
    selected = TextEncoding::BASE64;
    input.erase(0, 7);
  } else {
    bool allHex = !input.empty();
    for (const char value : input) {
      if (hexValue(value) < 0) {
        allHex = false;
        break;
      }
    }
    if (allHex && ((input.size() % 2) == 0)) {
      selected = TextEncoding::HEX;
    }
  }

  const bool decoded = (selected == TextEncoding::HEX) ? decodeHex(input, out, error)
                                                       : decodeBase64(input, out, error);
  if (!decoded) {
    return false;
  }
  if (encoding != nullptr) {
    *encoding = selected;
  }
  return true;
}

}  // namespace ProvisionTLV
}  // namespace Furble
