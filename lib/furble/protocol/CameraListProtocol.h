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

/**
 * Is a scanned camera the same physical camera as one already saved?
 *
 * The saved index is keyed on the BLE address, which is enough to overwrite the
 * right record but not enough to recognise a camera the user is pairing a
 * second time. A Fujifilm Secure body advertises a resolvable private address
 * that changes with every pairing, so the same camera comes back under a new
 * key and a second, useless saved entry appears. Identity here is the vendor
 * type plus either the address or the advertised name, which is the model and
 * body identifier the user reads on screen.
 *
 * Two identical bodies advertising the same name do collide. Refusing is the
 * safe side of that trade: the user can delete the saved entry and pair again,
 * whereas a silent duplicate leaves two records for one camera and the saved
 * reconnect picks whichever the index happens to hold.
 */
bool sameSavedIdentity(uint32_t type_a,
                       uint64_t address_a,
                       const std::string &name_a,
                       uint32_t type_b,
                       uint64_t address_b,
                       const std::string &name_b);

}  // namespace CameraListProtocol
}  // namespace Furble

#endif
