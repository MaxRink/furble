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
using Furble::CameraListProtocol::INDEX_HEADER;
using Furble::CameraListProtocol::INDEX_HEADER_BYTES;
using Furble::CameraListProtocol::INDEX_ID_INVALID;
using Furble::CameraListProtocol::INDEX_LEGACY_ENTRY_BYTES;
using Furble::CameraListProtocol::INDEX_NAME_BYTES;
using Furble::CameraListProtocol::IndexEntry;
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
IndexEntry makeEntry(const std::string &name, uint32_t type, uint8_t camera_id = 0) {
  IndexEntry entry = {};
  std::memset(entry.name, 0, INDEX_NAME_BYTES);
  std::memcpy(entry.name, name.data(), std::min(name.size(), INDEX_NAME_BYTES));
  entry.type = type;
  entry.camera_id = camera_id;
  return entry;
}

bool sameEntry(const IndexEntry &a, const IndexEntry &b) {
  return std::memcmp(a.name, b.name, INDEX_NAME_BYTES) == 0 && a.type == b.type
         && a.camera_id == b.camera_id;
}

// A v1 blob: bare records of name and little endian type, with no header.
std::vector<uint8_t> encodeLegacy(const std::vector<IndexEntry> &entries) {
  std::vector<uint8_t> bytes(entries.size() * INDEX_LEGACY_ENTRY_BYTES, 0x00);
  for (size_t i = 0; i < entries.size(); i++) {
    uint8_t *record = bytes.data() + i * INDEX_LEGACY_ENTRY_BYTES;
    std::memcpy(record, entries[i].name, INDEX_NAME_BYTES);
    for (size_t b = 0; b < sizeof(uint32_t); b++) {
      record[INDEX_NAME_BYTES + b] = static_cast<uint8_t>(entries[i].type >> (8 * b));
    }
  }
  return bytes;
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
      makeEntry("fujifilm-x100", 0x00000003, 7),
      makeEntry("sony-a7", 0x11223344, 42),
      makeEntry("", 0, 254),
  };

  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts a normal index");
  check(bytes.size() == INDEX_HEADER_BYTES + entries.size() * INDEX_ENTRY_BYTES,
        "encoded size matches the entry count plus the schema header");
  check(std::memcmp(bytes.data(), INDEX_HEADER, INDEX_HEADER_BYTES) == 0,
        "an encoded index opens with the v2 schema header");

  std::vector<IndexEntry> decoded;
  check(decodeIndex(bytes.data(), bytes.size(), decoded), "decodeIndex accepts its own output");
  check(decoded.size() == entries.size(), "decode preserves the entry count");
  for (size_t i = 0; i < entries.size(); i++) {
    check(sameEntry(entries[i], decoded[i]), "round trip preserves each entry byte for byte");
  }
}

void testWireLayout() {
  // The type is stored little endian in the four bytes after the name.
  std::vector<IndexEntry> entries = {makeEntry("cam", 0x04030201, 0x5a)};
  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts a single entry");
  check(bytes.size() == INDEX_HEADER_BYTES + INDEX_ENTRY_BYTES,
        "single entry is a header plus one record");
  const uint8_t *record = bytes.data() + INDEX_HEADER_BYTES;
  check(record[INDEX_NAME_BYTES + 0] == 0x01, "type byte 0 is the least significant");
  check(record[INDEX_NAME_BYTES + 1] == 0x02, "type byte 1 follows little endian order");
  check(record[INDEX_NAME_BYTES + 2] == 0x03, "type byte 2 follows little endian order");
  check(record[INDEX_NAME_BYTES + 3] == 0x04, "type byte 3 is the most significant");
  check(record[INDEX_NAME_BYTES + 4] == 0x5a, "the camera id trails the type");
  check(std::memcmp(record, "cam", 3) == 0, "the name is stored at the record start");
}

void testEmpty() {
  std::vector<IndexEntry> entries;
  std::vector<uint8_t> bytes;
  check(encodeIndex(entries, bytes), "encodeIndex accepts an empty index");
  check(bytes.size() == INDEX_HEADER_BYTES, "an empty index encodes to the header alone");

  std::vector<IndexEntry> empty;
  check(decodeIndex(bytes.data(), bytes.size(), empty), "a header with no records decodes");
  check(empty.empty(), "a header with no records decodes to no entries");

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

void testLegacyMigration() {
  // A v1 blob has no header and no ids. It must still decode, with every id
  // marked unassigned so CameraList allocates and persists one.
  const std::vector<IndexEntry> entries = {
      makeEntry("00AABBCC", 3),
      makeEntry("00DDEEFF", 9),
  };
  const auto legacy = encodeLegacy(entries);

  std::vector<IndexEntry> decoded;
  check(decodeIndex(legacy.data(), legacy.size(), decoded), "decodeIndex accepts a v1 blob");
  check(decoded.size() == entries.size(), "v1 decode preserves the entry count");
  check(decoded[0].type == 3 && decoded[1].type == 9, "v1 decode preserves each type");
  check(std::memcmp(decoded[0].name, "00AABBCC", 8) == 0, "v1 decode preserves each name");
  check(decoded[0].camera_id == INDEX_ID_INVALID && decoded[1].camera_id == INDEX_ID_INVALID,
        "v1 entries decode with no assigned id");

  // A v1 blob whose length also divides by the v2 record size must still decode
  // as v1, which is what the explicit header buys.
  std::vector<IndexEntry> ambiguous(21, makeEntry("cam", 1));
  const auto ambiguousBytes = encodeLegacy(ambiguous);
  check(ambiguousBytes.size() % INDEX_ENTRY_BYTES == 0, "the ambiguous blob divides both ways");
  std::vector<IndexEntry> ambiguousDecoded;
  check(decodeIndex(ambiguousBytes.data(), ambiguousBytes.size(), ambiguousDecoded),
        "an ambiguous length still decodes");
  check(ambiguousDecoded.size() == ambiguous.size(),
        "a headerless blob always decodes as v1 records");
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
  testLegacyMigration();
  testUpsert();

  if (g_failures > 0) {
    std::cerr << "camera list protocol tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "camera list protocol tests: PASS\n";
  return 0;
}
