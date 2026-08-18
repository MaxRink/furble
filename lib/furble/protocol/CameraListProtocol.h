#ifndef FURBLE_CAMERA_LIST_PROTOCOL_H
#define FURBLE_CAMERA_LIST_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Furble {
namespace CameraListProtocol {

constexpr size_t INDEX_NAME_BYTES = 16;
constexpr size_t INDEX_ENTRY_BYTES = INDEX_NAME_BYTES + sizeof(uint32_t);

struct IndexEntry {
  char name[INDEX_NAME_BYTES];
  uint32_t type;
};

static_assert(sizeof(IndexEntry) == INDEX_ENTRY_BYTES, "Unexpected camera index layout");

std::string addressKey(uint64_t address);

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes);
bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries);
void upsertIndex(std::vector<IndexEntry> &entries, const IndexEntry &entry);

}  // namespace CameraListProtocol
}  // namespace Furble

#endif
