#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "FurbleCompanionAuth.h"
#include "HostHmacSha256.h"

namespace {

using FurbleHostCrypto::hmacSha256;

uint8_t nonceSeed = 0;

bool nextNonce(uint8_t *nonce, size_t len) {
  if (nonce == nullptr || len != Furble::CompanionAuth::NONCE_SIZE) {
    return false;
  }
  for (size_t index = 0; index < len; index++) {
    nonce[index] = static_cast<uint8_t>(nonceSeed + index);
  }
  nonceSeed++;
  return true;
}

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string fixtureField(const std::string &path, const char *name) {
  std::ifstream input(path);
  require(input.good(), "companion auth fixture could not be opened");
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::string key = std::string("\"") + name + "\"";
  const size_t keyPos = text.find(key);
  require(keyPos != std::string::npos, "companion auth fixture field is missing");
  const size_t valueStart = text.find('"', text.find(':', keyPos) + 1);
  require(valueStart != std::string::npos, "companion auth fixture value is missing");
  const size_t valueEnd = text.find('"', valueStart + 1);
  require(valueEnd != std::string::npos, "companion auth fixture value is unterminated");
  return text.substr(valueStart + 1, valueEnd - valueStart - 1);
}

std::vector<uint8_t> decodeHex(const std::string &hex) {
  require((hex.size() % 2) == 0, "companion auth fixture hex has odd length");
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return bytes;
}

std::array<uint8_t, Furble::CompanionAuth::RESPONSE_SIZE> responseFor(
    const std::string &password,
    const std::array<uint8_t, Furble::CompanionAuth::NONCE_SIZE> &nonce) {
  std::array<uint8_t, Furble::CompanionAuth::HMAC_SIZE> digest = {};
  require(hmacSha256(reinterpret_cast<const uint8_t *>(password.data()), password.size(),
                     nonce.data(), nonce.size(), digest.data(), digest.size()),
          "host HMAC shim failed");
  std::array<uint8_t, Furble::CompanionAuth::RESPONSE_SIZE> response = {};
  std::copy_n(digest.begin(), response.size(), response.begin());
  return response;
}

void testAuthGate(void) {
  Furble::CompanionAuth auth {hmacSha256, nextNonce};
  auth.setPassword("correct horse battery staple");
  auth.onConnected();

  require(!auth.allowsProtected(), "protected request was accepted before authentication");
  std::array<uint8_t, Furble::CompanionAuth::NONCE_SIZE> nonce = {};
  require(auth.begin(nonce), "password configuration did not start a challenge");
  require(!auth.allowsProtected(), "challenge alone authenticated the connection");

  const auto response = responseFor("correct horse battery staple", nonce);
  require(auth.respond(response.data(), response.size())
              == Furble::CompanionAuth::response_t::AUTHENTICATED,
          "correct HMAC response was rejected");
  require(auth.allowsProtected(), "protected request was rejected after authentication");
  // A success consumes its nonce like a failure does. Without this the same
  // proof stays valid for every later respond() on the connection.
  require(!auth.hasChallenge(), "successful response left a replayable challenge live");
  {
    const auto again = responseFor("correct horse battery staple", nonce);
    Furble::CompanionAuth fresh {hmacSha256, nextNonce};
    require(fresh.setPassword("correct horse battery staple"), "replay guard setup failed");
    fresh.onConnected();
    require(
        fresh.respond(again.data(), again.size()) == Furble::CompanionAuth::response_t::REJECTED,
        "a proof replayed with no outstanding challenge was accepted");
    require(!fresh.allowsProtected(), "replayed proof unlocked the protected request");
  }

  auth.onDisconnected();
  require(!auth.allowsProtected(), "disconnect left the connection authenticated");
  require(!auth.hasChallenge(), "disconnect left a challenge nonce live");
  require(auth.failureCount() == 0, "disconnect left the failure counter live");

  auth.onConnected();
  require(auth.begin(nonce), "malformed-response test did not start a challenge");
  const uint8_t malformed = 0;
  require(auth.respond(&malformed, 1) == Furble::CompanionAuth::response_t::REJECTED,
          "malformed HMAC response was not rejected");
  require(!auth.hasChallenge(), "malformed response left a replayable challenge live");
  require(auth.failureCount() == 1, "malformed response did not consume a failure attempt");
  require(auth.begin(nonce), "malformed-response test did not issue a fresh challenge");
  const auto retry = responseFor("correct horse battery staple", nonce);
  require(
      auth.respond(retry.data(), retry.size()) == Furble::CompanionAuth::response_t::AUTHENTICATED,
      "fresh challenge after malformed response was rejected");

  auth.onConnected();
  require(auth.begin(nonce), "second connection did not start a challenge");
  std::array<uint8_t, Furble::CompanionAuth::RESPONSE_SIZE> wrong = {};
  require(auth.respond(wrong.data(), wrong.size()) == Furble::CompanionAuth::response_t::REJECTED,
          "wrong HMAC response was accepted");
  require(!auth.allowsProtected(), "wrong HMAC unlocked the protected request");
  require(auth.failureCount() == 1, "wrong HMAC did not increment the failure counter");

  const auto replay = responseFor("correct horse battery staple", nonce);
  auth.onConnected();
  require(auth.begin(nonce), "replay test did not start a challenge");
  require(auth.respond(replay.data(), replay.size()) == Furble::CompanionAuth::response_t::REJECTED,
          "response for a previous nonce was accepted");

  auth.onConnected();
  for (uint8_t attempt = 0; attempt < Furble::CompanionAuth::MAX_FAILURES; attempt++) {
    require(auth.begin(nonce), "rate-limit test did not start a challenge");
    const auto result = auth.respond(wrong.data(), wrong.size());
    if (attempt + 1 == Furble::CompanionAuth::MAX_FAILURES) {
      require(result == Furble::CompanionAuth::response_t::DROPPED,
              "failure limit did not drop the connection");
    } else {
      require(result == Furble::CompanionAuth::response_t::REJECTED,
              "intermediate wrong HMAC was not rejected");
    }
  }
  require(!auth.allowsProtected(), "dropped connection remained authorized");
  require(!auth.begin(nonce), "dropped connection accepted a new challenge");
  require(auth.isDropped(), "new challenge reset the dropped state");

  auth.setPassword("");
  auth.onConnected();
  require(auth.isAuthenticated(), "empty password did not authenticate the connection");
  require(!auth.begin(nonce), "empty password unexpectedly started a challenge");
  require(auth.allowsProtected(), "empty password rejected the protected request");

  auth.onConnected();
  require(!auth.setPassword(std::string(64, 'x')),
          "passwords over the 63-byte firmware limit are rejected");
  require(auth.isPasswordSet() == false,
          "an over-length password cannot replace the active secret");
  auth.onConnected();
  require(!auth.isAuthenticated(),
          "an over-length persisted password must not become an empty-password bypass");
  require(!auth.begin(nonce), "an invalid persisted password must not start a challenge");
  require(!auth.allowsProtected(), "an invalid persisted password must fail closed");
  require(auth.setPassword("recovered password"),
          "a valid password must recover from an invalid persisted value");
  auth.onConnected();
  require(auth.begin(nonce), "a recovered password did not start a challenge");
  require(!auth.setPassword(std::string("bad\0password", 12)),
          "passwords containing NUL must be rejected");
  auth.onConnected();
  require(!auth.allowsProtected(), "a NUL-containing persisted password must fail closed");
}

void testHmacVector(const std::string &fixturePath) {
  constexpr char key[] = "key";
  constexpr char message[] = "The quick brown fox jumps over the lazy dog";
  constexpr uint8_t expected[Furble::CompanionAuth::HMAC_SIZE] = {
      0xf7, 0xbc, 0x83, 0xf4, 0x30, 0x53, 0x84, 0x24, 0xb1, 0x32, 0x98,
      0xe6, 0xaa, 0x6f, 0xb1, 0x43, 0xef, 0x4d, 0x59, 0xa1, 0x49, 0x46,
      0x17, 0x59, 0x97, 0x47, 0x9d, 0xbc, 0x2d, 0x1a, 0x3c, 0xd8,
  };
  std::array<uint8_t, Furble::CompanionAuth::HMAC_SIZE> actual = {};
  require(hmacSha256(reinterpret_cast<const uint8_t *>(key), strlen(key),
                     reinterpret_cast<const uint8_t *>(message), strlen(message), actual.data(),
                     actual.size()),
          "HMAC vector calculation failed");
  require(std::equal(actual.begin(), actual.end(), expected),
          "host HMAC shim does not match SHA-256 HMAC");

  const auto nonce = decodeHex(fixtureField(fixturePath, "nonce"));
  const auto proof = decodeHex(fixtureField(fixturePath, "proof"));
  require(nonce.size() == Furble::CompanionAuth::NONCE_SIZE && proof.size() == 18,
          "companion auth fixture sizes are invalid");
  std::array<uint8_t, Furble::CompanionAuth::NONCE_SIZE> nonceArray = {};
  std::copy(nonce.begin(), nonce.end(), nonceArray.begin());
  const auto response = responseFor(fixtureField(fixturePath, "password_utf8"), nonceArray);
  require(std::equal(response.begin(), response.end(), proof.begin() + 2),
          "firmware HMAC does not match the shared companion fixture");
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "companion_auth_test requires its canonical fixture path");
    testHmacVector(argv[1]);
    testAuthGate();
  } catch (const std::exception &error) {
    std::cerr << "companion_auth_test: FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "companion_auth_test: PASS\n";
  return 0;
}
