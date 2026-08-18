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

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes) {
  if (entries.size() > std::numeric_limits<size_t>::max() / INDEX_ENTRY_BYTES) {
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
  }

  return true;
}

bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries) {
  entries.clear();
  if (bytes % INDEX_ENTRY_BYTES != 0 || (bytes != 0 && data == nullptr)) {
    return false;
  }

  entries.resize(bytes / INDEX_ENTRY_BYTES);
  for (size_t i = 0; i < entries.size(); ++i) {
    const size_t offset = i * INDEX_ENTRY_BYTES;
    std::memcpy(entries[i].name, data + offset, INDEX_NAME_BYTES);
    entries[i].type = static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES])
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 1]) << 8)
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 2]) << 16)
                      | (static_cast<uint32_t>(data[offset + INDEX_NAME_BYTES + 3]) << 24);
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
