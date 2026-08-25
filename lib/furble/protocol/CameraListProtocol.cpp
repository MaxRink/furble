#include <cstdio>
#include <cstring>
#include <limits>

#include "CameraListProtocol.h"

namespace Furble {
namespace CameraListProtocol {

std::string addressKey(uint64_t address) {
  if (address > 0xffffffffffffULL) {
    return {};
  }
  char key[INDEX_NAME_BYTES] = {0};
  std::snprintf(key, sizeof(key), "%08llX", static_cast<unsigned long long>(address));
  return std::string(key);
}

std::string typedAddressKey(uint64_t address, uint8_t addressType) {
  if (address > 0xffffffffffffULL || addressType > 0xf) {
    return {};
  }
  char key[INDEX_NAME_BYTES] = {0};
  std::snprintf(key, sizeof(key), "%012llX%X", static_cast<unsigned long long>(address),
                static_cast<unsigned>(addressType));
  return std::string(key);
}

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes) {
  if (entries.size() > MAX_INDEX_ENTRIES
      || entries.size() > std::numeric_limits<size_t>::max() / INDEX_ENTRY_BYTES) {
    return false;
  }

  bytes.assign(entries.size() * INDEX_ENTRY_BYTES, 0x00);
  for (size_t i = 0; i < entries.size(); ++i) {
    const size_t offset = i * INDEX_ENTRY_BYTES;
    std::memcpy(bytes.data() + offset, entries[i].name, INDEX_NAME_BYTES);
    const uint32_t type = entries[i].type;
    bytes[offset + INDEX_NAME_BYTES] = static_cast<uint8_t>(type);
    bytes[offset + INDEX_NAME_BYTES + 1] = static_cast<uint8_t>(type >> 8);
    bytes[offset + INDEX_NAME_BYTES + 2] = static_cast<uint8_t>(type >> 16);
    bytes[offset + INDEX_NAME_BYTES + 3] = static_cast<uint8_t>(type >> 24);
    bytes[offset + INDEX_NAME_BYTES + 4] = entries[i].camera_id;
  }

  return true;
}

uint32_t indexChecksum(const uint8_t *data, size_t bytes) {
  uint32_t checksum = 0xffffffffU;
  for (size_t i = 0; i < bytes; i++) {
    checksum ^= data[i];
    for (unsigned bit = 0; bit < 8; bit++) {
      checksum = (checksum >> 1) ^ (0xedb88320U & (0U - (checksum & 1U)));
    }
  }
  return checksum ^ 0xffffffffU;
}

bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries) {
  entries.clear();
  if (bytes != 0 && data == nullptr) {
    return false;
  }
  if (bytes == 0) {
    return true;
  }

  // Most lengths identify one layout, but the least common multiple is a real
  // possibility. Never infer the format from record contents in that case.
  const bool currentAligned = (bytes % INDEX_ENTRY_BYTES) == 0;
  const bool legacyAligned = (bytes % LEGACY_INDEX_ENTRY_BYTES) == 0;
  if (currentAligned == legacyAligned) {
    // A length such as 420 is divisible by both layouts. There is no safe
    // content heuristic for that case, so require the caller to provide the
    // persisted schema selected by CameraList migration.
    return false;
  }

  return decodeIndex(data, bytes, currentAligned ? IndexFormat::CURRENT : IndexFormat::LEGACY,
                     entries);
}

bool decodeIndex(const uint8_t *data,
                 size_t bytes,
                 IndexFormat format,
                 std::vector<IndexEntry> &entries) {
  entries.clear();
  if (bytes != 0 && data == nullptr) {
    return false;
  }

  const size_t entrySize =
      (format == IndexFormat::CURRENT) ? INDEX_ENTRY_BYTES : LEGACY_INDEX_ENTRY_BYTES;
  const size_t maxBytes =
      (format == IndexFormat::CURRENT) ? MAX_CURRENT_INDEX_BYTES : MAX_LEGACY_INDEX_BYTES;
  if ((bytes % entrySize) != 0 || bytes > maxBytes) {
    return false;
  }

  const size_t count = bytes / entrySize;
  entries.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t offset = i * entrySize;
    std::memcpy(entries[i].name, data + offset, INDEX_NAME_BYTES);
    entries[i].type = static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES])
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 1]) << 8)
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 2]) << 16)
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 3]) << 24);
    entries[i].camera_id =
        (entrySize == INDEX_ENTRY_BYTES) ? data[offset + INDEX_NAME_BYTES + 4] : 0;
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

void assignCameraIds(std::vector<IndexEntry> &entries) {
  // Continue above the highest id already assigned so a mix of migrated and
  // fresh entries never collides. Zero stays invalid.
  uint16_t next = 1;
  for (const auto &entry : entries) {
    if (entry.camera_id >= next) {
      next = static_cast<uint16_t>(entry.camera_id + 1);
    }
  }
  for (auto &entry : entries) {
    if ((entry.camera_id == 0) && (next <= 0xfe)) {
      entry.camera_id = static_cast<uint8_t>(next);
      next++;
    }
  }
}

}  // namespace CameraListProtocol
}  // namespace Furble
