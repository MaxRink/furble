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
 * Vendor type whose advertised address changes between pairings.
 *
 * A Fujifilm Secure body advertises a resolvable private address, so it comes
 * back under a new address every time it is paired. It is the only supported
 * vendor that does. The value is Camera::Type::FUJIFILM_SECURE; this module is
 * deliberately free of any Camera dependency, so CameraList.cpp carries the
 * static_assert that pins the two together.
 */
constexpr uint32_t ROTATING_ADDRESS_TYPE = 8;

/**
 * Is a scanned camera the same physical camera as one already saved?
 *
 * The saved index is keyed on the BLE address, which is enough to overwrite the
 * right record but not enough to recognise a camera the user is pairing a
 * second time. A Fujifilm Secure body comes back under a new key, so the same
 * camera gains a second, useless saved entry. Identity is therefore the vendor
 * type plus the address, and for ROTATING_ADDRESS_TYPE only, the advertised
 * name as a fallback.
 *
 * The name fallback is scoped to that one vendor on purpose. Every other camera
 * advertises a stable address, so for them a different address is a different
 * body. Matching on the name there would refuse a user who owns two bodies of
 * one model: the advertised name is the bare model, so a second X-T5 or a
 * second GR IV would read as already saved with no way to add it.
 *
 * Two Fujifilm Secure bodies advertising the same name still collide. Refusing
 * is the safe side of that trade: the user can delete the saved entry and pair
 * again, whereas a silent duplicate leaves two records for one camera and the
 * saved reconnect picks whichever the index happens to hold. PR #266 puts the
 * body serial in the advertised name, which resolves the collision.
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
