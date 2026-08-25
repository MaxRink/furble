#ifndef FURBLE_RESTART_MARKER_H
#define FURBLE_RESTART_MARKER_H

#include <cstdint>

namespace Furble {

/** Small storage seam used by NVS and by deterministic host fault tests. */
class RestartMarkerStorage {
 public:
  virtual ~RestartMarkerStorage() = default;
  virtual bool read(const char *key, uint32_t &value) = 0;
  virtual bool write(const char *key, uint32_t value) = 0;
  virtual bool remove(const char *key) = 0;
  virtual bool exists(const char *key) = 0;
};

class RestartMarker {
 public:
  static bool mark(RestartMarkerStorage &storage) {
    uint32_t generation = 0;
    if (storage.exists("cr_boot_gen") && !storage.read("cr_boot_gen", generation)) {
      return false;
    }
    const uint32_t target = generation == UINT32_MAX ? 1U : generation + 1U;
    if (!storage.write("cr_pending", target) || !verify(storage, "cr_pending", target)) {
      clear(storage);
      return false;
    }
    if (!storage.write("cr_commit", target) || !verify(storage, "cr_commit", target)) {
      clear(storage);
      return false;
    }
    return true;
  }

  static bool consume(RestartMarkerStorage &storage) {
    uint32_t generation = 0;
    if (storage.exists("cr_boot_gen") && !storage.read("cr_boot_gen", generation)) {
      return false;
    }
    const uint32_t boot = generation == UINT32_MAX ? 1U : generation + 1U;
    // Advance the boot generation before examining the token. A reset before
    // consumption therefore makes an old token ineligible on the next boot.
    if (!storage.write("cr_boot_gen", boot) || !verify(storage, "cr_boot_gen", boot)) {
      return false;
    }
    uint32_t pending = 0;
    uint32_t commit = 0;
    if (!storage.read("cr_pending", pending) || !storage.read("cr_commit", commit)
        || pending != boot || commit != boot) {
      return false;
    }
    // Consume both halves before granting the fast path. Any reset or NVS
    // error between these operations leaves an incomplete token and fails
    // closed on the next boot.
    if (!storage.remove("cr_commit") || storage.exists("cr_commit") || !storage.remove("cr_pending")
        || storage.exists("cr_pending")) {
      return false;
    }
    return true;
  }

 private:
  static bool verify(RestartMarkerStorage &storage, const char *key, uint32_t expected) {
    uint32_t actual = 0;
    return storage.read(key, actual) && actual == expected;
  }

  static void clear(RestartMarkerStorage &storage) {
    (void)storage.remove("cr_commit");
    (void)storage.remove("cr_pending");
  }
};

}  // namespace Furble

#endif
