#include <cstdio>
#include <cstring>
#include <limits>

#include "CameraListProtocol.h"

namespace Furble {
namespace CameraListProtocol {

std::string addressKey(uint64_t address) {
  char key[INDEX_NAME_BYTES] = {0};
  std::snprintf(key, sizeof(key), "%08llX", static_cast<unsigned long long>(address));
  return std::string(key);
}

namespace {

void putType(uint8_t *out, uint32_t type) {
  out[0] = static_cast<uint8_t>(type);
  out[1] = static_cast<uint8_t>(type >> 8);
  out[2] = static_cast<uint8_t>(type >> 16);
  out[3] = static_cast<uint8_t>(type >> 24);
}

uint32_t getType(const uint8_t *in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8)
         | (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

bool hasHeader(const uint8_t *data, size_t bytes) {
  return (bytes >= INDEX_HEADER_BYTES)
         && (std::memcmp(data, INDEX_HEADER, INDEX_HEADER_BYTES) == 0);
}

}  // namespace

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes) {
  if (entries.size()
      > (std::numeric_limits<size_t>::max() - INDEX_HEADER_BYTES) / INDEX_ENTRY_BYTES) {
    return false;
  }

  bytes.assign(INDEX_HEADER_BYTES + entries.size() * INDEX_ENTRY_BYTES, 0x00);
  std::memcpy(bytes.data(), INDEX_HEADER, INDEX_HEADER_BYTES);
  for (size_t i = 0; i < entries.size(); ++i) {
    uint8_t *record = bytes.data() + INDEX_HEADER_BYTES + i * INDEX_ENTRY_BYTES;
    std::memcpy(record, entries[i].name, INDEX_NAME_BYTES);
    putType(record + INDEX_NAME_BYTES, entries[i].type);
    record[INDEX_NAME_BYTES + sizeof(uint32_t)] = entries[i].camera_id;
  }

  return true;
}

bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries) {
  entries.clear();
  if (bytes != 0 && data == nullptr) {
    return false;
  }

  // A v1 blob carries no header and no camera ids. Those entries decode with
  // INDEX_ID_INVALID so the caller assigns and persists fresh ids once.
  if (!hasHeader(data, bytes)) {
    if (bytes % INDEX_LEGACY_ENTRY_BYTES != 0) {
      return false;
    }
    entries.resize(bytes / INDEX_LEGACY_ENTRY_BYTES);
    for (size_t i = 0; i < entries.size(); ++i) {
      const uint8_t *record = data + i * INDEX_LEGACY_ENTRY_BYTES;
      std::memcpy(entries[i].name, record, INDEX_NAME_BYTES);
      entries[i].type = getType(record + INDEX_NAME_BYTES);
      entries[i].camera_id = INDEX_ID_INVALID;
    }
    return true;
  }

  const size_t payload = bytes - INDEX_HEADER_BYTES;
  if (payload % INDEX_ENTRY_BYTES != 0) {
    return false;
  }

  entries.resize(payload / INDEX_ENTRY_BYTES);
  for (size_t i = 0; i < entries.size(); ++i) {
    const uint8_t *record = data + INDEX_HEADER_BYTES + i * INDEX_ENTRY_BYTES;
    std::memcpy(entries[i].name, record, INDEX_NAME_BYTES);
    entries[i].type = getType(record + INDEX_NAME_BYTES);
    entries[i].camera_id = record[INDEX_NAME_BYTES + sizeof(uint32_t)];
  }

  return true;
}

void upsertIndex(std::vector<IndexEntry> &entries, const IndexEntry &entry) {
  for (auto &existing : entries) {
    if (std::memcmp(existing.name, entry.name, INDEX_NAME_BYTES) == 0) {
      existing = entry;
      return;
    }
  }

  entries.push_back(entry);
}

}  // namespace CameraListProtocol
}  // namespace Furble
