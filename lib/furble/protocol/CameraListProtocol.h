#ifndef FURBLE_CAMERA_LIST_PROTOCOL_H
#define FURBLE_CAMERA_LIST_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Furble {
namespace CameraListProtocol {

constexpr size_t INDEX_NAME_BYTES = 16;
constexpr size_t INDEX_ENTRY_BYTES = INDEX_NAME_BYTES + sizeof(uint32_t) + sizeof(uint8_t);
// IDs 1 through 254 are the complete protocol range. Keeping the bound in
// the codec prevents malformed NVS lengths from causing unbounded allocation.
constexpr size_t MAX_INDEX_ENTRIES = 254;
constexpr size_t MAX_CURRENT_INDEX_BYTES = MAX_INDEX_ENTRIES * INDEX_ENTRY_BYTES;
// Layout used before the stable camera id was added. A blob written by that
// firmware has no id byte, so decodeIndex migrates it and leaves camera_id 0.
constexpr size_t LEGACY_INDEX_ENTRY_BYTES = INDEX_NAME_BYTES + sizeof(uint32_t);
constexpr size_t MAX_LEGACY_INDEX_BYTES = MAX_INDEX_ENTRIES * LEGACY_INDEX_ENTRY_BYTES;

enum class IndexFormat : uint8_t {
  LEGACY = 0,
  CURRENT = 1,
};

struct __attribute__((packed)) IndexEntry {
  char name[INDEX_NAME_BYTES];
  uint32_t type;
  uint8_t camera_id;
};

static_assert(sizeof(IndexEntry) == INDEX_ENTRY_BYTES, "Unexpected camera index layout");

std::string addressKey(uint64_t address);

// New camera records include the BLE address type in the NVS key. Twelve
// address digits plus one type digit fit the ESP-NVS fifteen-character key
// limit and distinguish public and random identities with equal address bits.
std::string typedAddressKey(uint64_t address, uint8_t addressType);

bool encodeIndex(const std::vector<IndexEntry> &entries, std::vector<uint8_t> &bytes);
uint32_t indexChecksum(const uint8_t *data, size_t bytes);
bool decodeIndex(const uint8_t *data, size_t bytes, std::vector<IndexEntry> &entries);
bool decodeIndex(const uint8_t *data,
                 size_t bytes,
                 IndexFormat format,
                 std::vector<IndexEntry> &entries);
void upsertIndex(std::vector<IndexEntry> &entries, const IndexEntry &entry);

/**
 * Give a stable id to every entry that still carries the invalid id zero.
 *
 * Entries decoded from a pre-id (legacy) blob come back with camera_id zero.
 * This walks them in order and assigns fresh ids above the highest id already
 * present, so an upgraded device exposes stable ids without losing any saved
 * camera. The walk is deterministic, so the same stored order yields the same
 * ids on every boot until the next save persists them. If all nonzero byte
 * values are already consumed, remaining entries retain zero rather than
 * reusing an id that belongs to another camera.
 */
void assignCameraIds(std::vector<IndexEntry> &entries);

}  // namespace CameraListProtocol
}  // namespace Furble

#endif
