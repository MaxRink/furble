// Unit tests for the saved camera index codec in
// lib/furble/protocol/CameraListProtocol.cpp. The module is pure byte
// construction with no BLE or NVS dependency, so it links straight into a
// standalone host executable. The tests exercise round trips, the on wire
// layout, the address key formatting, upsert semantics, and the negative
// paths that reject malformed input.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "protocol/CameraListProtocol.h"

using Furble::CameraListProtocol::addressKey;
using Furble::CameraListProtocol::decodeIndex;
using Furble::CameraListProtocol::encodeIndex;
using Furble::CameraListProtocol::INDEX_ENTRY_BYTES;
using Furble::CameraListProtocol::INDEX_NAME_BYTES;
using Furble::CameraListProtocol::IndexEntry;
using Furble::CameraListProtocol::sameSavedIdentity;
using Furble::CameraListProtocol::upsertIndex;

namespace {

int g_failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_failures++;
  }
}

// Build an entry with a padded name and a type code. The name is copied into
// the fixed width field the same way the production code stores it.
IndexEntry makeEntry(const std::string &name, uint32_t type) {
  IndexEntry entry = {};
  std::memset(entry.name, 0, INDEX_NAME_BYTES);
  std::memcpy(entry.name, name.data(), std::min(name.size(), INDEX_NAME_BYTES));
  entry.type = type;
  return entry;
}

bool sameEntry(const IndexEntry &a, const IndexEntry &b) {
  return std::memcmp(a.name, b.name, INDEX_NAME_BYTES) == 0 && a.type == b.type;
}

void testAddressKey() {
  // The key is an eight digit uppercase hex string of the lower address bits.
  check(addressKey(0) == "00000000", "zero address pads to eight hex digits");
  check(addressKey(0x1) == "00000001", "small address keeps leading zeroes");
  check(addressKey(0xDEADBEEF) == "DEADBEEF", "address renders uppercase hex");
  check(addressKey(0xABCDEF) == "00ABCDEF", "address shorter than eight digits pads");
  check(addressKey(0x004080C0FF).size() >= 8, "wider address grows past eight digits");
}

void testRoundTrip() {
  std::vector<IndexEntry> entries = {
      makeEntry("fujifilm-x100", 0x00000003),
      makeEntry("sony-a7", 0x11223344),
      makeEntry("", 0),
  };

  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts a normal index");
  check(bytes.size() == entries.size() * INDEX_ENTRY_BYTES, "encoded size matches the entry count");

  std::vector<IndexEntry> decoded;
  check(decodeIndex(bytes.data(), bytes.size(), decoded), "decodeIndex accepts its own output");
  check(decoded.size() == entries.size(), "decode preserves the entry count");
  for (size_t i = 0; i < entries.size(); i++) {
    check(sameEntry(entries[i], decoded[i]), "round trip preserves each entry byte for byte");
  }
}

void testWireLayout() {
  // The type is stored little endian in the four bytes after the name.
  std::vector<IndexEntry> entries = {makeEntry("cam", 0x04030201)};
  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts a single entry");
  check(bytes.size() == INDEX_ENTRY_BYTES, "single entry is one record wide");
  check(bytes[INDEX_NAME_BYTES + 0] == 0x01, "type byte 0 is the least significant");
  check(bytes[INDEX_NAME_BYTES + 1] == 0x02, "type byte 1 follows little endian order");
  check(bytes[INDEX_NAME_BYTES + 2] == 0x03, "type byte 2 follows little endian order");
  check(bytes[INDEX_NAME_BYTES + 3] == 0x04, "type byte 3 is the most significant");
  check(std::memcmp(bytes.data(), "cam", 3) == 0, "the name is stored at the record start");
}

void testEmpty() {
  std::vector<IndexEntry> entries;
  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts an empty index");
  check(bytes.empty(), "an empty index encodes to no bytes");

  std::vector<IndexEntry> decoded = {makeEntry("stale", 1)};
  check(decodeIndex(nullptr, 0, decoded), "decodeIndex accepts zero bytes with a null pointer");
  check(decoded.empty(), "decoding zero bytes clears the output vector");
}

void testRejects() {
  IndexEntry probe = {};
  std::vector<uint8_t> bytes;
  check(encodeIndex({probe}, bytes), "encodeIndex accepts a probe entry");

  std::vector<IndexEntry> decoded;
  // A buffer that is not a whole number of records must be rejected.
  check(!decodeIndex(bytes.data(), bytes.size() - 1, decoded),
        "decodeIndex rejects a length that is not a record multiple");
  check(!decodeIndex(bytes.data(), 1, decoded), "decodeIndex rejects a single stray byte");
  // A null pointer with a nonzero length is a programming error, not empty.
  check(!decodeIndex(nullptr, INDEX_ENTRY_BYTES, decoded),
        "decodeIndex rejects a null pointer with a nonzero length");
}

void testUpsert() {
  std::vector<IndexEntry> index;
  upsertIndex(index, makeEntry("alpha", 1));
  upsertIndex(index, makeEntry("beta", 2));
  check(index.size() == 2, "upsert appends distinct names");

  // A second entry with the same name replaces the type in place.
  upsertIndex(index, makeEntry("alpha", 9));
  check(index.size() == 2, "upsert does not grow the list on a name match");
  check(index[0].type == 9, "upsert replaces the matching entry in place");
  check(index[1].type == 2, "upsert leaves the other entry untouched");

  upsertIndex(index, makeEntry("gamma", 3));
  check(index.size() == 3, "a fresh name appends after replacements");
}

// The already-saved refusal. The saved index is keyed on the BLE address, which
// cannot recognise a camera the user is pairing a second time: a Fujifilm
// Secure body advertises a resolvable private address that changes with every
// pairing, so the same camera comes back under a new key and the list gains a
// second, useless record.
void testSameSavedIdentity() {
  constexpr uint32_t kSecure = 8;  // Camera::Type::FUJIFILM_SECURE
  constexpr uint32_t kBasic = 1;   // Camera::Type::FUJIFILM_BASIC
  const std::string name = "FUJIFILM X100VI";

  check(sameSavedIdentity(kSecure, 0x112233445566ULL, name, kSecure, 0x112233445566ULL, name),
        "the same address and type is the same camera");

  // The signature case: the body re-paired under a new resolvable private
  // address. Nothing but the advertised name survives, and it has to be enough.
  check(sameSavedIdentity(kSecure, 0xAABBCCDDEEFFULL, name, kSecure, 0x112233445566ULL, name),
        "a moved address still matches on the advertised name");

  check(!sameSavedIdentity(kSecure, 0xAABBCCDDEEFFULL, name, kSecure, 0x112233445566ULL,
                           "FUJIFILM X-T5"),
        "a different camera at a different address does not match");

  check(!sameSavedIdentity(kSecure, 0x112233445566ULL, name, kBasic, 0x112233445566ULL, name),
        "a different vendor mode is a different saved camera");

  // An unnamed advertisement carries no identity of its own, so it must never
  // match on the empty string and lock the user out of pairing.
  check(!sameSavedIdentity(kSecure, 0xAABBCCDDEEFFULL, "", kSecure, 0x112233445566ULL, ""),
        "an empty name never matches");
  check(!sameSavedIdentity(kSecure, 0xAABBCCDDEEFFULL, "", kSecure, 0x112233445566ULL, name),
        "an empty name does not match a real one");
}

// The name fallback exists only because a Fujifilm Secure body re-pairs under a
// new resolvable private address. Every other vendor keeps a stable address, so
// applying the fallback to them refuses a user who owns two bodies of the same
// model: the advertised name is the bare model, the second body reads as
// already saved, and there is no override. Multi-connect with two identical
// bodies, which the saved list supports, would become unreachable.
void testSecondBodyOfTheSameModelStaysPairable() {
  constexpr uint32_t kSecure = 8;  // Camera::Type::FUJIFILM_SECURE
  constexpr uint32_t kBasic = 1;   // Camera::Type::FUJIFILM_BASIC
  constexpr uint32_t kSony = 7;    // Camera::Type::SONY
  constexpr uint32_t kRicoh = 9;   // Camera::Type::RICOH

  const uint64_t first = 0x112233445566ULL;
  const uint64_t second = 0xAABBCCDDEEFFULL;

  check(!sameSavedIdentity(kRicoh, second, "GR IV", kRicoh, first, "GR IV"),
        "a second GR IV at its own address is a second camera, not the saved one");
  check(!sameSavedIdentity(kBasic, second, "FUJIFILM X-T5", kBasic, first, "FUJIFILM X-T5"),
        "a second X-T5 on the Basic protocol stays pairable");
  check(!sameSavedIdentity(kSony, second, "ILCE-7M4", kSony, first, "ILCE-7M4"),
        "a second Sony body of the same model stays pairable");

  // The one vendor that does rotate its address keeps the fallback, so the
  // re-pairing body is still recognised as the camera already saved.
  check(sameSavedIdentity(kSecure, second, "FUJIFILM X100VI", kSecure, first, "FUJIFILM X100VI"),
        "two Fujifilm Secure records with one name at two addresses are one camera");
}

}  // namespace

int main() {
  testAddressKey();
  testRoundTrip();
  testWireLayout();
  testEmpty();
  testRejects();
  testUpsert();
  testSameSavedIdentity();
  testSecondBodyOfTheSameModelStaysPairable();

  if (g_failures > 0) {
    std::cerr << "camera list protocol tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "camera list protocol tests: PASS\n";
  return 0;
}
