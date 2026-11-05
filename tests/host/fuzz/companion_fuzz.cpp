// Deterministic protocol fuzzer for the transport-independent companion GATT
// service.
//
// The production CompanionService, CompanionAuth and Control/Camera stack run
// over the same host doubles as companion_gatt_test. The fuzzer sends malformed
// auth, settings, camera, location and trigger packets, plus fragmented and
// oversized payloads, then checks bounded responses, auth gating and state
// recovery. The same seed always produces the same packet stream, so a finding
// replays exactly.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "FurbleCompanionAuth.h"
#include "FurbleCompanionService.h"
#include "FurbleControl.h"
#include "FurbleSettings.h"

namespace {

using Furble::CompanionAuth;
using Furble::CompanionService;

constexpr size_t MAX_PACKET_BYTES = 300;
constexpr const char *PASSWORD = "fuzz-pass";

int failures = 0;

void fail(const std::string &context, const std::string &message) {
  std::cerr << "COMPANION FUZZ FINDING [" << context << "] " << message << '\n';
  failures++;
}

void requireBounded(const std::string &context, const std::vector<uint8_t> &packet) {
  if (packet.size() > MAX_PACKET_BYTES) {
    fail(context, "response packet exceeds the bounded payload: " + std::to_string(packet.size()));
  }
}

class FuzzTransport final: public Furble::CompanionTransport {
 public:
  bool isConnected(void) const override { return connected; }
  bool isEncrypted(void) const override { return encrypted; }
  bool isAuthenticated(void) const override { return authenticated; }
  uint16_t getMaxPayload(void) const override { return 244; }

  void notify(uint8_t charId, const uint8_t *data, size_t len) override {
    if (data == nullptr) {
      fail("transport", "notify received a null pointer");
      return;
    }
    std::vector<uint8_t> packet(data, data + len);
    requireBounded("transport-notify", packet);
    notifications.push_back({charId, packet});
  }

  void indicate(uint8_t charId, const uint8_t *data, size_t len) override {
    if (data == nullptr) {
      fail("transport", "indicate received a null pointer");
      return;
    }
    std::vector<uint8_t> packet(data, data + len);
    requireBounded("transport-indicate", packet);
    indications.push_back({charId, packet});
  }

  void error(uint8_t, uint8_t) override {}

  void disconnect(void) override {
    connected = false;
    disconnects++;
  }

  struct packet_t {
    uint8_t charId;
    std::vector<uint8_t> value;
  };

  bool connected = true;
  bool encrypted = true;
  bool authenticated = true;
  size_t disconnects = 0;
  std::vector<packet_t> notifications;
  std::vector<packet_t> indications;

  void clear(void) {
    notifications.clear();
    indications.clear();
  }
};

CompanionAuth::response_t authenticate(CompanionService &service, FuzzTransport &transport) {
  transport.clear();
  service.onConnected();
  service.reloadPassword();
  const std::array<uint8_t, 2> begin = {CompanionService::AUTH_VERSION,
                                        CompanionService::AUTH_OP_BEGIN};
  service.handleAuth(begin.data(), begin.size());
  if (transport.indications.size() != 1
      || transport.indications.back().value.size() != CompanionService::AUTH_CHALLENGE_SIZE) {
    fail("auth", "begin did not produce one challenge");
    return CompanionAuth::response_t::REJECTED;
  }
  const auto &challenge = transport.indications.back().value;
  std::array<uint8_t, CompanionAuth::HMAC_SIZE> digest = {};
  if (!Furble::companionHmacSha256(reinterpret_cast<const uint8_t *>(PASSWORD),
                                   std::strlen(PASSWORD), challenge.data() + 2,
                                   CompanionAuth::NONCE_SIZE, digest.data(), digest.size())) {
    fail("auth", "HMAC computation failed");
    return CompanionAuth::response_t::REJECTED;
  }

  std::array<uint8_t, CompanionService::AUTH_PROOF_PACKET_SIZE> proof = {};
  proof[0] = CompanionService::AUTH_VERSION;
  proof[1] = CompanionService::AUTH_OP_PROOF;
  std::copy(digest.begin(), digest.begin() + CompanionAuth::RESPONSE_SIZE, proof.begin() + 2);
  transport.clear();
  service.handleAuth(proof.data(), proof.size());
  if (transport.indications.size() != 1
      || transport.indications.back().value.size() != CompanionService::AUTH_RESULT_SIZE
      || transport.indications.back().value[2] != CompanionService::AUTH_RESULT_AUTHENTICATED) {
    fail("auth", "valid proof was not acknowledged");
    return CompanionAuth::response_t::REJECTED;
  }
  return CompanionAuth::response_t::AUTHENTICATED;
}

std::vector<uint8_t> randomPacket(std::mt19937_64 &rng, size_t length) {
  std::vector<uint8_t> packet(length);
  for (auto &value : packet) {
    value = static_cast<uint8_t>(rng() & 0xff);
  }
  return packet;
}

std::vector<uint8_t> validLocation() {
  CompanionService::companion_fix_t packet = {};
  packet.version = 1;
  packet.flags = 0x07;
  packet.satellites = 7;
  packet.accuracy_m = 3;
  packet.latitude = 48.1234;
  packet.longitude = 11.5678;
  packet.altitude = 520.0;
  packet.year = 2026;
  packet.month = 11;
  packet.day = 5;
  packet.hour = 12;
  packet.minute = 34;
  packet.second = 56;
  packet.age_ms = 120;
  std::vector<uint8_t> bytes(sizeof(packet));
  std::memcpy(bytes.data(), &packet, sizeof(packet));
  return bytes;
}

void runCase(CompanionService &service,
             FuzzTransport &transport,
             const std::vector<uint8_t> &packet) {
  // Exercise every characteristic in a fixed rotation. A write to one handler
  // must never leak state into the next one.
  service.handleLocation(packet.data(), packet.size());
  service.handleSettings(packet.data(), packet.size());
  service.handleCameras(packet.data(), packet.size());
  service.handleTrigger(packet.data(), packet.size());
  service.handleAuth(packet.data(), packet.size());
  for (const auto &entry : transport.indications) {
    requireBounded("indication", entry.value);
  }
  for (const auto &entry : transport.notifications) {
    requireBounded("notification", entry.value);
  }
}

void runNullAndPartialWrites(CompanionService &service) {
  service.handleLocation(nullptr, 0);
  service.handleSettings(nullptr, 0);
  service.handleCameras(nullptr, 0);
  service.handleTrigger(nullptr, 0);
  service.handleAuth(nullptr, 0);
  const std::vector<uint8_t> empty;
  service.handleLocation(empty.data(), 0);
  service.handleSettings(empty.data(), 0);
  service.handleCameras(empty.data(), 0);
  service.handleTrigger(empty.data(), 0);
  service.handleAuth(empty.data(), 0);

  // A valid location prefix shorter than the full record, and a longer one.
  auto location = validLocation();
  for (size_t length = 0; length < location.size(); length++) {
    service.handleLocation(location.data(), length);
  }
  location.push_back(0xff);
  service.handleLocation(location.data(), location.size());
}

void runAuthGate(CompanionService &service, FuzzTransport &transport) {
  transport.clear();
  service.onConnected();
  service.reloadPassword();
  // Listing cameras is read-only by design and stays available on an
  // encrypted link. A camera mutation must not answer before the password
  // challenge succeeds.
  transport.clear();
  const std::vector<uint8_t> cameras = {CompanionService::CAMERA_OP_SELECT, 0};
  service.handleCameras(cameras.data(), cameras.size());
  for (const auto &entry : transport.indications) {
    if (entry.charId == Furble::COMPANION_CHAR_CAMERAS) {
      fail("auth-gate", "camera mutation answered before authentication");
    }
  }

  transport.clear();
  const std::vector<uint8_t> trigger = {1, 1};
  service.handleTrigger(trigger.data(), trigger.size());
  for (const auto &entry : transport.indications) {
    if (entry.charId == Furble::COMPANION_CHAR_SETTINGS
        || entry.charId == Furble::COMPANION_CHAR_CAMERAS) {
      fail("auth-gate", "protected characteristic answered before authentication");
    }
  }

  const auto result = authenticate(service, transport);
  if (result != CompanionAuth::response_t::AUTHENTICATED || !service.isPasswordAuthenticated()) {
    fail("auth-gate", "valid authentication did not unlock the service");
  }
}

void runPostFuzz(CompanionService &service, FuzzTransport &transport) {
  // After arbitrary malformed traffic the service must still accept a clean
  // reconnect, a valid proof and a settings list.
  transport.connected = true;
  transport.encrypted = true;
  transport.authenticated = true;
  if (authenticate(service, transport) != CompanionAuth::response_t::AUTHENTICATED) {
    return;
  }
  transport.clear();
  const std::vector<uint8_t> list = {0, 0, 0};
  service.handleSettings(list.data(), list.size());
  if (transport.indications.empty()) {
    fail("post-fuzz", "settings list produced no records");
    return;
  }
  const auto &last = transport.indications.back().value;
  if (last.size() < 5 || last[0] != 0 || last[1] != 0xff) {
    fail("post-fuzz", "settings list did not end in the terminator record");
  }
}

}  // namespace

int main(int argc, char **argv) {
  uint64_t seed = 1;
  uint32_t iterations = 300;
  std::string onlyCase;
  for (int index = 1; index < argc; index++) {
    const std::string argument = argv[index];
    if (argument == "--seed" && index + 1 < argc) {
      seed = std::strtoull(argv[++index], nullptr, 10);
    } else if (argument == "--iterations" && index + 1 < argc) {
      iterations = static_cast<uint32_t>(std::strtoul(argv[++index], nullptr, 10));
    } else if (argument == "--case" && index + 1 < argc) {
      onlyCase = argv[++index];
    } else {
      std::cerr << "usage: companion_fuzz [--seed N] [--iterations N] [--case NAME]\n";
      return 2;
    }
  }
  if (iterations == 0) {
    std::cerr << "iterations must be non-zero\n";
    return 2;
  }

  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, PASSWORD);
  FuzzTransport transport;
  CompanionService service(transport);
  service.onConnected();
  service.reloadPassword();
  transport.clear();

  runNullAndPartialWrites(service);
  runAuthGate(service, transport);

  std::mt19937_64 rng(seed);
  for (uint32_t iteration = 0; iteration < iterations; iteration++) {
    // Reset transport link state so every iteration starts from the same
    // reachable service state. Auth failures intentionally drop the link.
    transport.connected = true;
    transport.encrypted = true;
    transport.authenticated = true;
    transport.clear();
    service.onConnected();
    service.reloadPassword();

    const uint32_t mode = static_cast<uint32_t>(rng() % 8);
    std::vector<uint8_t> packet;
    std::string context = "iteration-" + std::to_string(iteration);
    switch (mode) {
      case 0:
        packet = randomPacket(rng, static_cast<size_t>(rng() % 64));
        break;
      case 1:
        packet = randomPacket(rng, static_cast<size_t>(rng() % 400));
        break;
      case 2:
        packet = validLocation();
        if (!packet.empty()) {
          packet[static_cast<size_t>(rng() % packet.size())] ^=
              static_cast<uint8_t>(1U << (rng() % 8));
        }
        break;
      case 3:
      {
        // A syntactically valid settings write with a random wire id, op and
        // declared length, including a length that disagrees with the payload.
        const uint8_t length = static_cast<uint8_t>(rng() % 64);
        packet = {static_cast<uint8_t>(rng() % 4), static_cast<uint8_t>(rng() % 80), length};
        const size_t actual = static_cast<size_t>(rng() % 64);
        const auto body = randomPacket(rng, actual);
        packet.insert(packet.end(), body.begin(), body.end());
        break;
      }
      case 4:
      {
        packet = {static_cast<uint8_t>(rng() % 8), static_cast<uint8_t>(rng() % 6)};
        if (packet[1] == 4) {
          const size_t extra = static_cast<size_t>(rng() % 8);
          const auto body = randomPacket(rng, extra);
          packet.insert(packet.end(), body.begin(), body.end());
        }
        break;
      }
      case 5:
      {
        packet = {CompanionService::AUTH_VERSION};
        const auto body = randomPacket(rng, static_cast<size_t>(rng() % 40));
        packet.insert(packet.end(), body.begin(), body.end());
        break;
      }
      case 6:
        packet = randomPacket(rng, MAX_PACKET_BYTES);
        break;
      default:
        packet = randomPacket(rng, static_cast<size_t>(rng() % 2));
        break;
    }
    runCase(service, transport, packet);

    // A transport drop must not lose the ability to reconnect and answer.
    if (!transport.connected) {
      transport.connected = true;
      service.onConnected();
      service.reloadPassword();
    }
  }

  runPostFuzz(service, transport);

  std::cout << "COMPANION FUZZ SUMMARY seed=" << seed << " iterations=" << iterations
            << " findings=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
