#include "FurbleCompanion.h"
#include "protocol_common.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using FurbleProtocolTest::Bytes;
using FurbleProtocolTest::Fix;
using FurbleProtocolTest::settingFile;
using FurbleProtocolTest::SettingInfo;
using FurbleProtocolTest::Status;
using FurbleProtocolTest::WireType;

static_assert(sizeof(Fix) == 42, "location wire size changed");
static_assert(sizeof(Status) == 20, "status wire size changed");

namespace {

struct ManifestEntry {
  std::string file;
  size_t length;
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

Bytes readBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot read " + path.string());
  }
  input.seekg(0, std::ios::end);
  const auto length = input.tellg();
  require(length >= 0, "cannot determine size of " + path.string());
  input.seekg(0, std::ios::beg);
  Bytes bytes(static_cast<size_t>(length));
  input.read(reinterpret_cast<char *>(bytes.data()), length);
  require(input.good() || input.eof(), "cannot read all of " + path.string());
  return bytes;
}

Bytes fixture(const fs::path &golden, const std::string &file) {
  return readBytes(golden / file);
}

void expectBytes(const Bytes &actual, const Bytes &expected, const std::string &name) {
  if (actual != expected) {
    std::ostringstream message;
    message << name << " differs from its expected wire bytes (got " << actual.size()
            << " bytes, expected " << expected.size() << ')';
    fail(message.str());
  }
}

Fix makeLocation(uint8_t flags,
                 uint8_t satellites,
                 uint8_t accuracy,
                 double latitude,
                 double longitude,
                 double altitude,
                 uint16_t year,
                 uint8_t month,
                 uint8_t day,
                 uint8_t hour,
                 uint8_t minute,
                 uint8_t second,
                 uint8_t centisecond,
                 uint32_t ageMs) {
  Fix fix = {};
  fix.version = 1;
  fix.flags = flags;
  fix.satellites = satellites;
  fix.accuracy_m = accuracy;
  fix.latitude = latitude;
  fix.longitude = longitude;
  fix.altitude = altitude;
  fix.year = year;
  fix.month = month;
  fix.day = day;
  fix.hour = hour;
  fix.minute = minute;
  fix.second = second;
  fix.centisecond = centisecond;
  fix.age_ms = ageMs;
  return fix;
}

Status makeStatus(uint8_t batteryPercent,
                  uint16_t batteryMv,
                  int16_t batteryMa,
                  uint8_t powerFlags,
                  uint8_t cameraTotal,
                  uint8_t cameraConnected,
                  uint8_t controlState,
                  uint8_t gpsSource,
                  uint8_t gpsSatellites,
                  uint8_t intervalometerState,
                  uint16_t intervalometerRemaining,
                  uint32_t uptimeSeconds) {
  Status status = {};
  status.version = 1;
  status.battery_percent = batteryPercent;
  status.battery_mv = batteryMv;
  status.battery_ma = batteryMa;
  status.power_flags = powerFlags;
  status.camera_total = cameraTotal;
  status.camera_connected = cameraConnected;
  status.control_state = controlState;
  status.gps_source = gpsSource;
  status.gps_satellites = gpsSatellites;
  status.ivl_state = intervalometerState;
  status.ivl_remaining = intervalometerRemaining;
  status.uptime_s = uptimeSeconds;
  return status;
}

std::vector<ManifestEntry> readManifest(const fs::path &golden) {
  std::ifstream input(golden / "corpus.json");
  require(static_cast<bool>(input), "cannot read golden/corpus.json");
  const std::regex entry_pattern(
      "\\{\"file\":\"([^\"]+)\",\"kind\":\"[^\"]+\",\"length\":([0-9]+),"
      "\"valid\":(true|false),\"description\":\"");
  std::vector<ManifestEntry> entries;
  std::string line;
  while (std::getline(input, line)) {
    std::smatch match;
    if (std::regex_search(line, match, entry_pattern)) {
      entries.push_back({match[1].str(), static_cast<size_t>(std::stoul(match[2].str()))});
    }
  }
  require(entries.size() > 100, "golden corpus is unexpectedly small");
  return entries;
}

void checkManifest(const fs::path &golden) {
  const auto entries = readManifest(golden);
  std::set<std::string> listed;
  for (const auto &entry : entries) {
    require(entry.file.find("..") == std::string::npos, "manifest path escapes golden directory");
    require(listed.insert(entry.file).second, "duplicate manifest entry: " + entry.file);
    const auto bytes = fixture(golden, entry.file);
    require(bytes.size() == entry.length, "manifest length mismatch for " + entry.file);
  }

  size_t binaryCount = 0;
  for (const auto &entry : fs::recursive_directory_iterator(golden)) {
    if (!entry.is_regular_file() || (entry.path().extension() != ".bin")) {
      continue;
    }
    ++binaryCount;
    const auto relative = fs::relative(entry.path(), golden).generic_string();
    require(listed.count(relative) == 1, "binary is missing from corpus.json: " + relative);
  }
  require(binaryCount == entries.size(), "corpus.json has a non-binary or missing fixture entry");
}

void checkLocations(const fs::path &golden) {
  expectBytes(
      fixture(golden, "location/min.bin"),
      FurbleProtocolTest::bytesOf(makeLocation(0, 0, 255, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0)),
      "location/min.bin");
  expectBytes(fixture(golden, "location/full.bin"),
              FurbleProtocolTest::bytesOf(makeLocation(0x07, 7, 12, 12.25, -45.5, 123.75, 2026, 8,
                                                       16, 14, 15, 16, 17, 0x01020304)),
              "location/full.bin");
  expectBytes(fixture(golden, "location/max.bin"),
              FurbleProtocolTest::bytesOf(makeLocation(0x07, 255, 254, 90.0, -180.0, 8848.86,
                                                       0xffff, 12, 31, 23, 59, 60, 99, 0xffffffff)),
              "location/max.bin");

  const auto invalid = fixture(golden, "location/invalid-flags.bin");
  require(invalid.size() == sizeof(Fix), "location invalid fixture size changed");
  require(invalid[0] == 1 && invalid[1] == 0x80, "location invalid flag fixture changed");
}

void checkStatuses(const fs::path &golden) {
  expectBytes(fixture(golden, "status/zero.bin"),
              FurbleProtocolTest::bytesOf(makeStatus(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)),
              "status/zero.bin");
  expectBytes(fixture(golden, "status/snapshot.bin"),
              FurbleProtocolTest::bytesOf(
                  makeStatus(85, 4120, -120, 3, 2, 1, 4, 2, 9, 5, 0xffff, 0x01020304)),
              "status/snapshot.bin");
  expectBytes(
      fixture(golden, "status/sentinels.bin"),
      FurbleProtocolTest::bytesOf(makeStatus(255, 0xffff, std::numeric_limits<int16_t>::min(), 3,
                                             255, 255, 255, 255, 255, 255, 0xffff, 0xffffffff)),
      "status/sentinels.bin");

  const auto sentinels = fixture(golden, "status/sentinels.bin");
  require(sentinels[1] == 255, "battery unknown sentinel changed");
  require(sentinels[2] == 0xff && sentinels[3] == 0xff, "battery voltage sentinel changed");
  require(sentinels[13] == 0xff && sentinels[14] == 0xff, "interval remaining sentinel changed");
}

Bytes settingsRequest(uint8_t operation, uint8_t id, const Bytes &value) {
  Bytes request = {operation, id, static_cast<uint8_t>(value.size())};
  request.insert(request.end(), value.begin(), value.end());
  return request;
}

bool isValidSettingsRequest(const Bytes &request) {
  if (request.size() < 3) {
    return false;
  }
  const uint8_t operation = request[0];
  const uint8_t length = request[2];
  if ((operation > 2) || (request.size() != static_cast<size_t>(3 + length))) {
    return false;
  }
  return (operation > 1) || (length == 0);
}

bool isValidStandardResponse(const Bytes &response) {
  return response.size() >= 4 && response.size() >= static_cast<size_t>(4 + response[3]);
}

bool isValidListResponse(const Bytes &response) {
  // List records carry the standard status,id,type,length,value header plus one
  // trailing flags byte: minimum size is 4 header bytes + length + 1 flags.
  return response.size() >= 4 && response.size() >= static_cast<size_t>(5 + response[3]);
}

void checkSettings(const std::string &root, const fs::path &golden) {
  const auto settings = FurbleProtocolTest::parseSettings(root);
  std::set<uint8_t> exposedIds;
  for (const auto &setting : settings) {
    if (setting.wire_id == 0) {
      continue;
    }
    require(exposedIds.insert(setting.wire_id).second,
            "duplicate wire id in src/FurbleSettings.cpp");
    const auto value = FurbleProtocolTest::sampleValue(setting);
    const uint8_t type = FurbleProtocolTest::wireTypeCode(setting.type);
    const uint8_t flags = FurbleProtocolTest::settingListFlags(setting);

    expectBytes(fixture(golden, settingFile("request-get", setting.wire_id)),
                settingsRequest(1, setting.wire_id, {}), setting.symbol + " get request");
    expectBytes(fixture(golden, settingFile("request-set", setting.wire_id)),
                settingsRequest(2, setting.wire_id, value), setting.symbol + " set request");

    const auto get = fixture(golden, settingFile("response-get", setting.wire_id));
    require(isValidStandardResponse(get), setting.symbol + " get response is not a TLV");
    require(get[0] == 0 && get[1] == setting.wire_id && get[2] == type && get[3] == value.size(),
            setting.symbol + " get response header changed");
    expectBytes(Bytes(get.begin() + 4, get.end()), value, setting.symbol + " get response value");

    const auto list = fixture(golden, settingFile("response-list", setting.wire_id));
    require(isValidListResponse(list), setting.symbol + " list response is not a list TLV");
    require(
        list[0] == 0 && list[1] == setting.wire_id && list[2] == type && list[3] == value.size(),
        setting.symbol + " list response header changed");
    expectBytes(Bytes(list.begin() + 4, list.begin() + 4 + value.size()), value,
                setting.symbol + " list response value");
    require(
        list.size() == static_cast<size_t>(4 + value.size() + 1) && list[4 + value.size()] == flags,
        setting.symbol + " list response trailing flags changed");

    expectBytes(fixture(golden, settingFile("response-set", setting.wire_id)),
                {0, setting.wire_id, type, 0}, setting.symbol + " set response");
  }
  require(exposedIds.size() >= 20, "too few exposed settings in source table");

  const auto listRequest = fixture(golden, "settings/request-list.bin");
  require(isValidSettingsRequest(listRequest) && listRequest == Bytes({0, 0, 0}),
          "list request changed");
  require(isValidSettingsRequest(fixture(golden, "settings/request-get-unknown.bin")),
          "unknown get request is malformed");
  require(isValidSettingsRequest(fixture(golden, "settings/request-get-hidden.bin")),
          "hidden get request is malformed");
  require(!isValidSettingsRequest(fixture(golden, "settings/request-bad-length.bin")),
          "bad length request was accepted");
  require(!isValidSettingsRequest(fixture(golden, "settings/request-list-value.bin")),
          "list value request was accepted");
  require(!isValidSettingsRequest(fixture(golden, "settings/request-unknown-op.bin")),
          "unknown operation request was accepted");

  const auto unknown = fixture(golden, "settings/response-unknown-id.bin");
  const auto badLength = fixture(golden, "settings/response-bad-length.bin");
  const auto readOnly = fixture(golden, "settings/response-read-only.bin");
  const auto rejected = fixture(golden, "settings/response-rejected.bin");
  require(isValidStandardResponse(unknown) && unknown[0] == 1, "unknown id response changed");
  require(isValidStandardResponse(badLength) && badLength[0] == 2, "bad length response changed");
  require(isValidStandardResponse(readOnly) && readOnly[0] == 3, "read only response changed");
  require(isValidStandardResponse(rejected) && rejected[0] == 4, "rejected response changed");
  const std::set<uint8_t> statuses = {unknown[0], badLength[0], readOnly[0], rejected[0]};
  require(statuses == std::set<uint8_t>({1, 2, 3, 4}), "settings status coverage changed");

  const auto terminator = fixture(golden, "settings/response-terminator.bin");
  require(isValidListResponse(terminator) && terminator == Bytes({0, 0xff, 4, 0, 0}),
          "settings list terminator changed");
  require(!isValidStandardResponse(fixture(golden, "settings/response-truncated.bin")),
          "truncated settings response was accepted");
}

void checkTriggers(const fs::path &golden) {
  const char *names[] = {"release", "shutter-press", "focus-press", "focus-release", "timed"};
  for (uint8_t operation = 0; operation <= 4; ++operation) {
    const uint16_t holdMs = operation == 4 ? 300 : 0;
    expectBytes(fixture(golden, "trigger/" + std::string(names[operation]) + ".bin"),
                {1, operation, static_cast<uint8_t>(holdMs & 0xff),
                 static_cast<uint8_t>((holdMs >> 8) & 0xff)},
                "trigger operation " + std::to_string(operation));
  }
  expectBytes(fixture(golden, "trigger/timed-max.bin"), {1, 4, 0xff, 0xff},
              "trigger/timed-max.bin");
  require(fixture(golden, "trigger/invalid-version.bin") == Bytes({0, 0, 0, 0}),
          "invalid trigger version fixture changed");
  require(fixture(golden, "trigger/short.bin").size() < 4, "short trigger fixture changed");
  require(fixture(golden, "trigger/timed-short.bin").size() < 4,
          "short timed trigger fixture changed");
  require(fixture(golden, "trigger/unknown-op.bin")[1] == 5,
          "unknown trigger operation fixture changed");
}

std::unordered_map<std::string, std::string> parseUuidMap(const std::string &path, bool kotlin) {
  const std::string source = FurbleProtocolTest::readText(path);
  const std::regex pattern =
      kotlin ? std::regex(
                   "(val|const\\s+val)\\s+"
                   "(SERVICE_UUID|LOCATION_UUID|STATUS_UUID|SETTINGS_UUID|TRIGGER_UUID)"
                   "\\s*:\\s*UUID\\s*=\\s*UUID\\.fromString\\(\"([0-9a-fA-F-]+)\"\\)")
             : std::regex(
                   "static\\s+constexpr\\s+const\\s+char\\s*\\*\\s*"
                   "(SERVICE_UUID|LOCATION_UUID|STATUS_UUID|SETTINGS_UUID|TRIGGER_UUID)"
                   "\\s*=\\s*\"([0-9a-fA-F-]+)\"");
  std::unordered_map<std::string, std::string> uuids;
  for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) {
    const size_t nameIndex = kotlin ? 2 : 1;
    const size_t uuidIndex = kotlin ? 3 : 2;
    uuids[(*it)[nameIndex].str()] = (*it)[uuidIndex].str();
  }
  return uuids;
}

std::string uuidPrefix(const std::string &uuid) {
  require(uuid.size() >= 8, "UUID is too short");
  return uuid.substr(0, 8);
}

void checkUuids(const std::string &root) {
  const auto cpp = parseUuidMap(root + "/include/FurbleCompanion.h", false);
  const auto kotlin = parseUuidMap(
      root + "/companion/android/app/src/main/java/com/furble/companion/protocol/FurbleProtocol.kt",
      true);
  const std::vector<std::string> names = {"SERVICE_UUID", "LOCATION_UUID", "STATUS_UUID",
                                          "SETTINGS_UUID", "TRIGGER_UUID"};
  require(cpp.size() == names.size(), "C++ companion UUID declarations are incomplete");
  require(kotlin.size() == names.size(), "Kotlin companion UUID declarations are incomplete");

  const std::string baseSuffix = cpp.at("SERVICE_UUID").substr(8);
  for (size_t index = 0; index < names.size(); ++index) {
    const auto &name = names[index];
    require(cpp.at(name) == kotlin.at(name), "UUID mismatch for " + name);
    require(cpp.at(name).substr(8) == baseSuffix, "UUID suffix drift for " + name);
    std::ostringstream expectedPrefix;
    expectedPrefix << "b57f4f" << std::hex << std::setfill('0') << std::setw(2) << (0x5e + index);
    require(uuidPrefix(cpp.at(name)) == expectedPrefix.str(), "UUID offset drift for " + name);
  }
  require(uuidPrefix(cpp.at("SERVICE_UUID")) == "b57f4f5e", "UUID base drifted");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " REPOSITORY_ROOT\n";
    return 2;
  }

  try {
    const std::string root = fs::absolute(argv[1]).string();
    const fs::path golden = fs::path(root) / "tests/protocol/golden";
    checkManifest(golden);
    checkLocations(golden);
    checkStatuses(golden);
    checkSettings(root, golden);
    checkTriggers(golden);
    checkUuids(root);
    std::cout << "protocol conformance passed: 42-byte location, 20-byte status, settings TLVs, "
                 "triggers, and UUIDs\n";
  } catch (const std::exception &error) {
    std::cerr << "protocol conformance failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
