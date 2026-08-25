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
#include <utility>
#include <vector>

#include "protocol/CameraListProtocol.h"

using Furble::CameraListProtocol::addressKey;
using Furble::CameraListProtocol::assignCameraIds;
using Furble::CameraListProtocol::decodeIndex;
using Furble::CameraListProtocol::encodeIndex;
using Furble::CameraListProtocol::INDEX_ENTRY_BYTES;
using Furble::CameraListProtocol::INDEX_NAME_BYTES;
using Furble::CameraListProtocol::IndexEntry;
using Furble::CameraListProtocol::LEGACY_INDEX_ENTRY_BYTES;
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
  // Two records so the deliberately bad lengths below are not a whole multiple
  // of either the current or the legacy record size.
  std::vector<IndexEntry> entries = {makeEntry("alpha", 1), makeEntry("beta", 2)};
  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts probe entries");

  std::vector<IndexEntry> decoded;
  // A buffer that is not a whole number of records must be rejected. The length
  // 2 * 21 - 1 = 41 is a multiple of neither 21 nor the legacy 20.
  check(!decodeIndex(bytes.data(), bytes.size() - 1, decoded),
        "decodeIndex rejects a length that is not a record multiple");
  check(!decodeIndex(bytes.data(), 1, decoded), "decodeIndex rejects a single stray byte");
  // A null pointer with a nonzero length is a programming error, not empty.
  check(!decodeIndex(nullptr, INDEX_ENTRY_BYTES, decoded),
        "decodeIndex rejects a null pointer with a nonzero length");
}

// Build a pre-id index blob: fixed width name then a little endian type, with
// no trailing id byte. This is exactly what firmware wrote before the stable id.
std::vector<uint8_t> makeLegacyBlob(const std::vector<std::pair<std::string, uint32_t>> &items) {
  std::vector<uint8_t> bytes(items.size() * LEGACY_INDEX_ENTRY_BYTES, 0x00);
  for (size_t i = 0; i < items.size(); i++) {
    const size_t offset = i * LEGACY_INDEX_ENTRY_BYTES;
    std::memcpy(bytes.data() + offset, items[i].first.data(),
                std::min(items[i].first.size(), INDEX_NAME_BYTES));
    const uint32_t type = items[i].second;
    bytes[offset + INDEX_NAME_BYTES + 0] = static_cast<uint8_t>(type);
    bytes[offset + INDEX_NAME_BYTES + 1] = static_cast<uint8_t>(type >> 8);
    bytes[offset + INDEX_NAME_BYTES + 2] = static_cast<uint8_t>(type >> 16);
    bytes[offset + INDEX_NAME_BYTES + 3] = static_cast<uint8_t>(type >> 24);
  }
  return bytes;
}

void testLegacyMigration() {
  const std::vector<uint8_t> blob = makeLegacyBlob({
      {"fujifilm-x100", 0x00000003},
      {"sony-a7",       0x11223344}
  });
  check(blob.size() == 2 * LEGACY_INDEX_ENTRY_BYTES, "legacy blob is two id-less records");

  std::vector<IndexEntry> decoded;
  check(decodeIndex(blob.data(), blob.size(), decoded),
        "decodeIndex accepts a legacy id-less blob");
  // No data loss: every saved camera survives the size change.
  check(decoded.size() == 2, "legacy decode loses no saved camera");
  check(std::memcmp(decoded[0].name, "fujifilm-x100", 13) == 0,
        "legacy decode keeps the first name");
  check(decoded[0].type == 0x00000003, "legacy decode keeps the first type");
  check(decoded[1].type == 0x11223344, "legacy decode keeps the second type");
  check(decoded[0].camera_id == 0 && decoded[1].camera_id == 0,
        "legacy entries decode without an id");
}

void testAssignCameraIds() {
  // makeEntry leaves camera_id zero, so these are all fresh.
  std::vector<IndexEntry> fresh = {makeEntry("a", 1), makeEntry("b", 2), makeEntry("c", 3)};
  assignCameraIds(fresh);
  check(fresh[0].camera_id == 1 && fresh[1].camera_id == 2 && fresh[2].camera_id == 3,
        "assignCameraIds numbers fresh entries from one");

  // An existing id is kept and new ids continue above the highest.
  std::vector<IndexEntry> mixed = {makeEntry("keep", 1), makeEntry("new", 2)};
  mixed[0].camera_id = 5;
  mixed[1].camera_id = 0;
  assignCameraIds(mixed);
  check(mixed[0].camera_id == 5, "assignCameraIds keeps an existing id");
  check(mixed[1].camera_id == 6, "assignCameraIds continues above the highest id");

  // A second pass is a no-op once every entry already carries an id.
  std::vector<IndexEntry> settled = mixed;
  assignCameraIds(settled);
  check(settled[0].camera_id == 5 && settled[1].camera_id == 6,
        "assignCameraIds leaves fully numbered entries untouched");

  std::vector<IndexEntry> exhausted = {makeEntry("last", 1), makeEntry("overflow", 2)};
  exhausted[0].camera_id = 0xff;
  assignCameraIds(exhausted);
  check(exhausted[0].camera_id == 0xff && exhausted[1].camera_id == 0,
        "assignCameraIds never reuses an id after the byte range is exhausted");
}

void testMigrationFullFlow() {
  // Decode an old blob, assign ids, and confirm the ids persist on re-encode.
  const std::vector<uint8_t> blob = makeLegacyBlob({
      {"cam-a", 1},
      {"cam-b", 2},
      {"cam-c", 3}
  });
  std::vector<IndexEntry> decoded;
  check(decodeIndex(blob.data(), blob.size(), decoded), "full flow decodes the legacy blob");
  assignCameraIds(decoded);
  check(decoded.size() == 3, "full flow keeps every saved camera");
  check(decoded[0].camera_id == 1 && decoded[1].camera_id == 2 && decoded[2].camera_id == 3,
        "full flow assigns a stable id to each migrated camera");

  std::vector<uint8_t> reencoded;
  check(encodeIndex(decoded, reencoded), "full flow re-encodes with ids");
  check(reencoded.size() == 3 * INDEX_ENTRY_BYTES, "re-encoded blob uses the current record size");
  std::vector<IndexEntry> reread;
  check(decodeIndex(reencoded.data(), reencoded.size(), reread),
        "full flow re-reads the migrated blob");
  check(reread.size() == 3 && reread[0].camera_id == 1 && reread[2].camera_id == 3,
        "persisted ids survive a decode round trip");
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

}  // namespace

int main() {
  testAddressKey();
  testRoundTrip();
  testWireLayout();
  testEmpty();
  testRejects();
  testUpsert();
  testLegacyMigration();
  testAssignCameraIds();
  testMigrationFullFlow();

  if (g_failures > 0) {
    std::cerr << "camera list protocol tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "camera list protocol tests: PASS\n";
  return 0;
}
