#ifndef FURBLE_RESTART_MARKER_H
#define FURBLE_RESTART_MARKER_H

#include <cstdint>

namespace Furble {

/** Small storage seam used by NVS and by deterministic host fault tests. */
class RestartMarkerStorage {
 public:
  enum class result : uint8_t { ERROR, ABSENT, PRESENT, SUCCESS };

  virtual ~RestartMarkerStorage() = default;
  virtual result read(const char *key, uint32_t &value) = 0;
  virtual result write(const char *key, uint32_t value) = 0;
  virtual result remove(const char *key) = 0;
  virtual result exists(const char *key) = 0;
};

class RestartMarker {
 public:
  static bool mark(RestartMarkerStorage &storage) {
    uint32_t generation = 0;
    const auto state = storage.exists("cr_boot_gen");
    if (state == RestartMarkerStorage::result::ERROR) {
      return false;
    }
    if (state == RestartMarkerStorage::result::PRESENT
        && storage.read("cr_boot_gen", generation) != RestartMarkerStorage::result::PRESENT) {
      return false;
    }
    const uint32_t target = generation == UINT32_MAX ? 1U : generation + 1U;
    if (storage.write("cr_pending", target) != RestartMarkerStorage::result::SUCCESS
        || !verify(storage, "cr_pending", target)) {
      clear(storage);
      return false;
    }
    if (storage.write("cr_commit", target) != RestartMarkerStorage::result::SUCCESS
        || !verify(storage, "cr_commit", target)) {
      clear(storage);
      return false;
    }
    return true;
  }

  static bool consume(RestartMarkerStorage &storage) {
    const auto poisonState = storage.exists("cr_poison");
    if (poisonState == RestartMarkerStorage::result::ERROR) {
      (void)storage.write("cr_poison", 1);
      return false;
    }
    if (poisonState == RestartMarkerStorage::result::PRESENT) {
      (void)storage.remove("cr_poison");
      return false;
    }
    uint32_t generation = 0;
    const auto state = storage.exists("cr_boot_gen");
    if (state == RestartMarkerStorage::result::ERROR) {
      (void)storage.write("cr_poison", 1);
      return false;
    }
    if (state == RestartMarkerStorage::result::PRESENT
        && storage.read("cr_boot_gen", generation) != RestartMarkerStorage::result::PRESENT) {
      return false;
    }
    const uint32_t boot = generation == UINT32_MAX ? 1U : generation + 1U;
    // Advance the boot generation before examining the token. A reset before
    // consumption therefore makes an old token ineligible on the next boot.
    if (storage.write("cr_boot_gen", boot) != RestartMarkerStorage::result::SUCCESS
        || !verify(storage, "cr_boot_gen", boot)) {
      (void)storage.write("cr_poison", 1);
      return false;
    }
    uint32_t pending = 0;
    uint32_t commit = 0;
    if (storage.read("cr_pending", pending) != RestartMarkerStorage::result::PRESENT
        || storage.read("cr_commit", commit) != RestartMarkerStorage::result::PRESENT
        || pending != boot || commit != boot) {
      return false;
    }
    // Consume both halves before granting the fast path. Any reset or NVS
    // error between these operations leaves an incomplete token and fails
    // closed on the next boot.
    if (storage.remove("cr_commit") != RestartMarkerStorage::result::SUCCESS
        || storage.exists("cr_commit") != RestartMarkerStorage::result::ABSENT
        || storage.remove("cr_pending") != RestartMarkerStorage::result::SUCCESS
        || storage.exists("cr_pending") != RestartMarkerStorage::result::ABSENT) {
      return false;
    }
    return true;
  }

 private:
  static bool verify(RestartMarkerStorage &storage, const char *key, uint32_t expected) {
    uint32_t actual = 0;
    return storage.read(key, actual) == RestartMarkerStorage::result::PRESENT && actual == expected;
  }

  static void clear(RestartMarkerStorage &storage) {
    (void)storage.remove("cr_commit");
    (void)storage.remove("cr_pending");
  }
};

}  // namespace Furble

#endif
