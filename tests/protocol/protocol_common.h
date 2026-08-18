#ifndef FURBLE_PROTOCOL_COMMON_H
#define FURBLE_PROTOCOL_COMMON_H

#include "FurbleCompanion.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace FurbleProtocolTest {

using Bytes = std::vector<uint8_t>;

using Fix = Furble::Companion::companion_fix_t;
using Status = Furble::Companion::companion_status_t;

static_assert(sizeof(Fix) == 42, "location wire size changed");
static_assert(sizeof(Status) == 20, "status wire size changed");
static_assert(offsetof(Fix, latitude) == 4, "location latitude offset changed");
static_assert(offsetof(Fix, longitude) == 12, "location longitude offset changed");
static_assert(offsetof(Fix, altitude) == 20, "location altitude offset changed");
static_assert(offsetof(Fix, age_ms) == 37, "location age offset changed");
static_assert(offsetof(Fix, reserved_tail) == 41, "location trailing byte offset changed");
static_assert(offsetof(Status, battery_mv) == 2, "status battery voltage offset changed");
static_assert(offsetof(Status, battery_ma) == 4, "status battery current offset changed");
static_assert(offsetof(Status, uptime_s) == 15, "status uptime offset changed");
static_assert(offsetof(Status, reserved_tail) == 19, "status trailing byte offset changed");

enum class WireType : uint8_t {
  BOOL = 0,
  U8 = 1,
  U32 = 2,
  STRING = 3,
  BLOB = 4,
};

struct SettingInfo {
  std::string symbol;
  uint8_t wire_id;
  WireType type;
};

template <typename T>
Bytes bytesOf(const T &value) {
  Bytes bytes(sizeof(value));
  std::memcpy(bytes.data(), &value, sizeof(value));
  return bytes;
}

inline std::string readText(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

inline WireType wireTypeFromName(const std::string &name) {
  if (name == "BOOL") {
    return WireType::BOOL;
  }
  if (name == "U8") {
    return WireType::U8;
  }
  if (name == "U32") {
    return WireType::U32;
  }
  if (name == "STRING") {
    return WireType::STRING;
  }
  if (name == "BLOB") {
    return WireType::BLOB;
  }
  throw std::runtime_error("unknown setting wire type " + name);
}

inline uint8_t wireTypeCode(WireType type) {
  return static_cast<uint8_t>(type);
}

inline std::string wireTypeName(WireType type) {
  switch (type) {
    case WireType::BOOL:
      return "BOOL";
    case WireType::U8:
      return "U8";
    case WireType::U32:
      return "U32";
    case WireType::STRING:
      return "STRING";
    case WireType::BLOB:
      return "BLOB";
  }
  throw std::runtime_error("invalid setting wire type");
}

inline std::unordered_map<std::string, WireType> parseSettingTypes(const std::string &root) {
  const std::string source = readText(root + "/src/FurbleCompanionService.cpp");
  const size_t begin = source.find("CompanionService::settingType");
  const size_t end = source.find("bool CompanionService::settingValue", begin);
  if ((begin == std::string::npos) || (end == std::string::npos) || (begin >= end)) {
    throw std::runtime_error("cannot find CompanionService::settingType in source");
  }

  const std::string function = source.substr(begin, end - begin);
  const std::regex case_pattern(R"(case\s+Settings::([A-Za-z0-9_]+)\s*:)");
  const std::regex return_pattern(R"(return\s+SETTING_([A-Za-z0-9_]+)\s*;)");
  std::unordered_map<std::string, WireType> types;
  std::vector<std::string> pending;
  std::istringstream lines(function);
  std::string line;
  while (std::getline(lines, line)) {
    std::smatch match;
    if (std::regex_search(line, match, case_pattern)) {
      pending.push_back(match[1].str());
    }
    if (std::regex_search(line, match, return_pattern)) {
      const WireType type = wireTypeFromName(match[1].str());
      for (const auto &symbol : pending) {
        types.emplace(symbol, type);
      }
      pending.clear();
    }
  }
  return types;
}

inline std::vector<SettingInfo> parseSettings(const std::string &root) {
  const std::string source = readText(root + "/src/FurbleSettings.cpp");
  const size_t begin = source.find("Settings::m_Setting = {");
  const size_t end = source.find("\n};", begin);
  if ((begin == std::string::npos) || (end == std::string::npos) || (begin >= end)) {
    throw std::runtime_error("cannot find Settings::m_Setting table in source");
  }

  const std::string table = source.substr(begin, end - begin);
  const std::regex row_pattern(R"(\{\s*([A-Za-z0-9_]+)\s*,\s*([0-9]+)\s*,)");
  const auto types = parseSettingTypes(root);
  std::vector<SettingInfo> settings;
  for (std::sregex_iterator it(table.begin(), table.end(), row_pattern), end_it; it != end_it;
       ++it) {
    const std::string symbol = (*it)[1].str();
    const unsigned long id = std::stoul((*it)[2].str());
    if (id > 0xff) {
      throw std::runtime_error("wire id is larger than one byte for " + symbol);
    }
    const auto type = types.find(symbol);
    if (type == types.end()) {
      throw std::runtime_error("no wire type for setting " + symbol);
    }
    settings.push_back({symbol, static_cast<uint8_t>(id), type->second});
  }
  if (settings.empty()) {
    throw std::runtime_error("settings table is empty");
  }
  return settings;
}

inline Bytes appendUint32LittleEndian(uint32_t value) {
  return {static_cast<uint8_t>(value & 0xff), static_cast<uint8_t>((value >> 8) & 0xff),
          static_cast<uint8_t>((value >> 16) & 0xff), static_cast<uint8_t>((value >> 24) & 0xff)};
}

inline std::string settingFile(const std::string &operation, uint8_t id) {
  std::ostringstream name;
  name << "settings/" << operation << '-' << std::setfill('0') << std::setw(2)
       << static_cast<unsigned>(id) << ".bin";
  return name.str();
}

inline Bytes sampleValue(const SettingInfo &setting) {
  switch (setting.type) {
    case WireType::BOOL:
      return {static_cast<uint8_t>((setting.wire_id & 1) != 0)};
    case WireType::U8:
      return {static_cast<uint8_t>(0x20 + setting.wire_id)};
    case WireType::U32:
      return appendUint32LittleEndian(0x01020300u + setting.wire_id);
    case WireType::STRING:
      return {'D', 'e', 'f', 'a', 'u', 'l', 't'};
    case WireType::BLOB:
    {
      Furble::interval_t interval = {};
      interval.count.value = 10;
      interval.count.unit = Furble::SpinValue::UNIT_NIL;
      interval.delay.value = 15;
      interval.delay.unit = Furble::SpinValue::UNIT_SEC;
      interval.shutter.value = 30;
      interval.shutter.unit = Furble::SpinValue::UNIT_MS;
      interval.wait.value = 0;
      interval.wait.unit = Furble::SpinValue::UNIT_SEC;
      return bytesOf(interval);
    }
  }
  throw std::runtime_error("invalid setting type");
}

}  // namespace FurbleProtocolTest

#endif
