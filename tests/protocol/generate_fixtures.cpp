#include "FurbleCompanion.h"
#include "protocol_common.h"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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
static_assert(std::endian::native == std::endian::little, "golden payloads require little endian");

namespace {

struct ManifestEntry {
  std::string file;
  std::string kind;
  size_t length;
  bool valid;
  std::string description;
  std::string direction;
  std::string expectedHex;
};

std::string directionFor(const std::string &kind) {
  if ((kind == "location") || (kind == "settings-request") || (kind == "trigger")) {
    return "central_to_peripheral";
  }
  return "peripheral_to_central";
}

std::string hexBytes(const Bytes &bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const uint8_t byte : bytes) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

void writeBytes(const fs::path &golden, const std::string &file, const Bytes &bytes) {
  const fs::path path = golden / file;
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write " + path.string());
  }
  output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (!output) {
    throw std::runtime_error("cannot finish writing " + path.string());
  }
}

void addFixture(const fs::path &golden,
                std::vector<ManifestEntry> &manifest,
                const std::string &file,
                const std::string &kind,
                const Bytes &bytes,
                bool valid,
                const std::string &description) {
  writeBytes(golden, file, bytes);
  manifest.push_back(
      {file, kind, bytes.size(), valid, description, directionFor(kind), hexBytes(bytes)});
}

std::string jsonEscape(const std::string &value) {
  std::string escaped;
  for (const char character : value) {
    if ((character == '\\') || (character == '"')) {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

void writeManifest(const fs::path &golden, const std::vector<ManifestEntry> &manifest) {
  std::ofstream output(golden / "corpus.json", std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write " + (golden / "corpus.json").string());
  }
  output << "{\n";
  output << "  \"format_version\": 1,\n";
  output << "  \"fixtures\": [\n";
  for (size_t index = 0; index < manifest.size(); ++index) {
    const auto &entry = manifest[index];
    output << "    {\"file\":\"" << jsonEscape(entry.file) << "\",\"kind\":\""
           << jsonEscape(entry.kind) << "\",\"length\":" << entry.length
           << ",\"valid\":" << (entry.valid ? "true" : "false") << ",\"description\":\""
           << jsonEscape(entry.description) << "\",\"direction\":\"" << jsonEscape(entry.direction)
           << "\",\"expected_bytes\":\"" << entry.expectedHex << "\"}";
    if (index + 1 != manifest.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << "  ]\n";
  output << "}\n";
}

Bytes makeSettingsRequest(uint8_t operation, uint8_t id, const Bytes &value) {
  if (value.size() > 0xff) {
    throw std::runtime_error("settings sample is too large");
  }
  Bytes request = {operation, id, static_cast<uint8_t>(value.size())};
  request.insert(request.end(), value.begin(), value.end());
  return request;
}

Bytes makeSettingsResponse(uint8_t status,
                           uint8_t id,
                           WireType type,
                           const Bytes &value,
                           bool listRecord,
                           uint8_t flags = 0) {
  if (value.size() > 0xff) {
    throw std::runtime_error("settings sample is too large");
  }
  Bytes response = {status, id, FurbleProtocolTest::wireTypeCode(type)};
  if (listRecord) {
    response.push_back(flags);
  }
  response.push_back(static_cast<uint8_t>(value.size()));
  response.insert(response.end(), value.begin(), value.end());
  return response;
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
  fix.reserved = 0;
  fix.age_ms = ageMs;
  fix.reserved_tail = 0;
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
  status.reserved_tail = 0;
  return status;
}

void generateLocations(const fs::path &golden, std::vector<ManifestEntry> &manifest) {
  addFixture(
      golden, manifest, "location/min.bin", "location",
      FurbleProtocolTest::bytesOf(makeLocation(0, 0, 255, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0)),
      true, "no valid fix with unknown accuracy");
  addFixture(golden, manifest, "location/full.bin", "location",
             FurbleProtocolTest::bytesOf(makeLocation(0x07, 7, 12, 12.25, -45.5, 123.75, 2026, 8,
                                                      16, 14, 15, 16, 17, 0x01020304)),
             true, "position, time, and altitude are valid");
  addFixture(golden, manifest, "location/max.bin", "location",
             FurbleProtocolTest::bytesOf(makeLocation(0x07, 255, 254, 90.0, -180.0, 8848.86, 0xffff,
                                                      12, 31, 23, 59, 60, 99, 0xffffffff)),
             true, "field boundary values");
  addFixture(golden, manifest, "location/invalid-flags.bin", "location",
             FurbleProtocolTest::bytesOf(
                 makeLocation(0x80, 1, 255, 0.0, 0.0, 0.0, 2026, 1, 1, 0, 0, 0, 0, 0)),
             false, "unknown flag bit must be rejected");
}

void generateStatuses(const fs::path &golden, std::vector<ManifestEntry> &manifest) {
  addFixture(golden, manifest, "status/zero.bin", "status",
             FurbleProtocolTest::bytesOf(makeStatus(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)), true,
             "zero-valued status fields");
  addFixture(golden, manifest, "status/snapshot.bin", "status",
             FurbleProtocolTest::bytesOf(
                 makeStatus(85, 4120, -120, 3, 2, 1, 4, 2, 9, 5, 0xffff, 0x01020304)),
             true, "normal status snapshot");
  addFixture(
      golden, manifest, "status/sentinels.bin", "status",
      FurbleProtocolTest::bytesOf(makeStatus(255, 0xffff, std::numeric_limits<int16_t>::min(), 3,
                                             255, 255, 255, 255, 255, 255, 0xffff, 0xffffffff)),
      true, "unknown and maximum status sentinels");
}

void generateSettings(const std::string &root,
                      const fs::path &golden,
                      std::vector<ManifestEntry> &manifest) {
  const auto settings = FurbleProtocolTest::parseSettings(root);
  addFixture(golden, manifest, "settings/request-list.bin", "settings-request",
             makeSettingsRequest(0, 0, {}), true, "list every exposed setting");

  for (const auto &setting : settings) {
    if (setting.wire_id == 0) {
      continue;
    }
    const Bytes value = FurbleProtocolTest::sampleValue(setting);
    const uint8_t flags = setting.symbol == "THEME" ? 1 : 0;
    addFixture(golden, manifest, settingFile("request-get", setting.wire_id), "settings-request",
               makeSettingsRequest(1, setting.wire_id, {}), true,
               "get setting " + setting.symbol + " by stable wire id");
    addFixture(golden, manifest, settingFile("request-set", setting.wire_id), "settings-request",
               makeSettingsRequest(2, setting.wire_id, value), true,
               "set setting " + setting.symbol + " by stable wire id");
    addFixture(golden, manifest, settingFile("response-get", setting.wire_id), "settings-response",
               makeSettingsResponse(0, setting.wire_id, setting.type, value, false), true,
               "get response for " + setting.symbol);
    addFixture(golden, manifest, settingFile("response-list", setting.wire_id),
               "settings-list-record",
               makeSettingsResponse(0, setting.wire_id, setting.type, value, true, flags), true,
               "list response for " + setting.symbol);
    addFixture(golden, manifest, settingFile("response-set", setting.wire_id), "settings-response",
               makeSettingsResponse(0, setting.wire_id, setting.type, {}, false), true,
               "set acknowledgement for " + setting.symbol);
  }

  addFixture(golden, manifest, "settings/request-get-unknown.bin", "settings-request",
             makeSettingsRequest(1, 0xfe, {}), true, "unknown ids are well-formed requests");
  addFixture(golden, manifest, "settings/request-get-hidden.bin", "settings-request",
             makeSettingsRequest(1, 0, {}), true, "wire id zero is hidden");
  addFixture(golden, manifest, "settings/request-bad-length.bin", "settings-request", {2, 7, 2, 1},
             false, "declared value length does not match bytes");
  addFixture(golden, manifest, "settings/request-list-value.bin", "settings-request", {0, 0, 1, 0},
             false, "list requests cannot carry a value");
  addFixture(golden, manifest, "settings/request-unknown-op.bin", "settings-request", {3, 1, 0},
             false, "unknown operation is rejected");

  addFixture(golden, manifest, "settings/response-unknown-id.bin", "settings-response",
             makeSettingsResponse(1, 0xfe, WireType::BLOB, {}, false), true, "unknown id response");
  addFixture(golden, manifest, "settings/response-bad-length.bin", "settings-response",
             makeSettingsResponse(2, 7, WireType::BLOB, {}, false), true, "bad length response");
  addFixture(golden, manifest, "settings/response-read-only.bin", "settings-response",
             makeSettingsResponse(3, 3, WireType::STRING, {}, false), true,
             "read only response status");
  addFixture(golden, manifest, "settings/response-rejected.bin", "settings-response",
             makeSettingsResponse(4, 5, WireType::BOOL, {}, false), true,
             "rejected response status");
  addFixture(golden, manifest, "settings/response-terminator.bin", "settings-list-record",
             makeSettingsResponse(0, 0xff, WireType::BLOB, {}, true), true, "end of list marker");
  addFixture(golden, manifest, "settings/response-truncated.bin", "settings-response",
             {0, 1, 1, 2, 0x20}, false, "declared response value is truncated");
}

void generateTriggers(const fs::path &golden, std::vector<ManifestEntry> &manifest) {
  const char *names[] = {"release", "shutter-press", "focus-press", "focus-release", "timed"};
  for (uint8_t operation = 0; operation <= 4; ++operation) {
    const uint16_t holdMs = operation == 4 ? 300 : 0;
    addFixture(golden, manifest, "trigger/" + std::string(names[operation]) + ".bin", "trigger",
               {1, operation, static_cast<uint8_t>(holdMs & 0xff),
                static_cast<uint8_t>((holdMs >> 8) & 0xff)},
               true, "trigger operation " + std::to_string(operation));
  }
  addFixture(golden, manifest, "trigger/timed-max.bin", "trigger", {1, 4, 0xff, 0xff}, true,
             "maximum timed hold");
  addFixture(golden, manifest, "trigger/invalid-version.bin", "trigger", {0, 0, 0, 0}, false,
             "version zero is rejected");
  addFixture(golden, manifest, "trigger/short.bin", "trigger", {1, 0}, false,
             "short trigger packet is rejected");
  addFixture(golden, manifest, "trigger/timed-short.bin", "trigger", {1, 4, 0}, false,
             "timed trigger needs a hold field");
  addFixture(golden, manifest, "trigger/unknown-op.bin", "trigger", {1, 5, 0, 0}, false,
             "unknown trigger operation is rejected");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " REPOSITORY_ROOT GOLDEN_DIRECTORY\n";
    return 2;
  }

  try {
    const std::string root = fs::absolute(argv[1]).string();
    const fs::path golden = fs::absolute(argv[2]);
    fs::create_directories(golden);
    std::vector<ManifestEntry> manifest;
    generateLocations(golden, manifest);
    generateStatuses(golden, manifest);
    generateSettings(root, golden, manifest);
    generateTriggers(golden, manifest);
    writeManifest(golden, manifest);
    std::cout << "generated " << manifest.size() << " golden fixtures in " << golden << '\n';
  } catch (const std::exception &error) {
    std::cerr << "fixture generation failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
