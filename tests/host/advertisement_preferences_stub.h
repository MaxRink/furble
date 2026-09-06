#ifndef FURBLE_HOST_PREFERENCES_STUB_H
#define FURBLE_HOST_PREFERENCES_STUB_H

// In-memory stand-in for NVS. Enough for CameraList to save an index, migrate
// it and read it back, without an ESP-IDF partition.

#include <cstddef>

namespace Furble {
namespace Host {

/** Drop every stored key, simulating a wiped device. */
void clearPreferences(void);

/** Number of stored keys, for tests that assert a key was written or removed. */
size_t preferencesKeyCount(void);

}  // namespace Host
}  // namespace Furble

#endif
