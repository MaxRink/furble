#ifndef FURBLE_CAMERA_LIST_PROTOCOL_H
#define FURBLE_CAMERA_LIST_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Furble {
namespace CameraListProtocol {

constexpr size_t INDEX_NAME_BYTES = 16;
/** Wire size of a v1 record: name and type only. */
constexpr size_t INDEX_LEGACY_ENTRY_BYTES = INDEX_NAME_BYTES + sizeof(uint32_t);
/** Wire size of a v2 record: v1 plus the stable camera id. */
constexpr size_t INDEX_ENTRY_BYTES = INDEX_LEGACY_ENTRY_BYTES + sizeof(uint8_t);

constexpr size_t INDEX_HEADER_BYTES = 4;

/**
 * Explicit v2 schema marker.
 *
 * A v1 blob is a bare array of records whose first byte opens an uppercase
 * hexadecimal address key, so 0xff can never start one. The marker keeps
 * migration unambiguous: without it a blob whose length divides by both record
 * sizes would decode two ways.
 */
constexpr uint8_t INDEX_HEADER[INDEX_HEADER_BYTES] = {0xff, 'C', 'L', 0x02};

/** Camera id reserved to mean not yet assigned. */
constexpr uint8_t INDEX_ID_INVALID = 0x00;
/** Camera id reserved on the companion wire to mean all cameras. */
constexpr uint8_t INDEX_ID_ALL = 0xff;

struct IndexEntry {
  char name[INDEX_NAME_BYTES];
  uint32_t type;
  uint8_t camera_id;
};

std::string addressKey(uint64_t address);

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes);
bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries);
void upsertIndex(std::vector<IndexEntry> &entries, const IndexEntry &entry);

}  // namespace CameraListProtocol
}  // namespace Furble

#endif
