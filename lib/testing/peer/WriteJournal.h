#pragma once

#include <mutex>

namespace Furble::Host {

/**
 * Serialize virtual-camera write journals.
 *
 * The peers append to their `writes()` vectors from the simulator's camera and
 * transport tasks. The `ble.peer.<n>` queries read the same journals from the
 * UI task. One process-wide mutex keeps the append and the snapshot from
 * overlapping; the host tests never write concurrently, so a single lock is
 * enough and cheaper than a per-peer lock object.
 */
inline std::mutex &peerWriteJournalMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace Furble::Host
