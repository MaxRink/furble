#ifndef FURBLE_HOST_PREFERENCES_STUB_H
#define FURBLE_HOST_PREFERENCES_STUB_H

// In-memory stand-in for NVS. Enough for CameraList to save an index, migrate
// it and read it back, without an ESP-IDF partition.

#include <cstddef>

namespace Furble {
namespace Host {

/** Drop every stored key, simulating a wiped device. */
void clearPreferences(void);

/** Fail the next Preferences transaction open. */
void failNextBegin(void);

/** Fail the next write for this key, returning a short write. */
void failNextPutForKey(const char *key);

/** Fail the next erase for this key. */
void failNextRemoveForKey(const char *key);

/** Number of stored keys, for tests that assert a key was written or removed. */
size_t preferencesKeyCount(void);

}  // namespace Host
}  // namespace Furble

#endif
