#ifndef FURBLE_BT_DEBUG_H
#define FURBLE_BT_DEBUG_H

#include <cstdint>

namespace Furble {

// Bluetooth debug tooling is firmware-only. The simulator has no radio.
class BtDebug {
 public:
  enum class PairMode : uint8_t {
    NONE,
    JUST_WORKS,
    NUMERIC_DISPLAY,
  };

  static bool startScan(uint32_t, bool) { return false; }
  static bool stopScan(void) { return false; }
  static bool startExplore(const char *, PairMode, bool) { return false; }
  static bool stopExplore(bool) { return false; }
  static bool readExplore(void) { return false; }
  static bool pairConfirm(bool) { return false; }
  static bool pairKey(uint32_t) { return false; }
  static bool isExploreRunning(void) { return false; }
};

}  // namespace Furble

#endif
