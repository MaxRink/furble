#ifndef FURBLE_BT_DEBUG_H
#define FURBLE_BT_DEBUG_H

#include <cstdint>

namespace Furble {

/** Developer-only Bluetooth scan, explorer, pairing, and journal commands. */
class BtDebug {
 public:
  enum class PairMode : uint8_t {
    NONE,
    JUST_WORKS,
    NUMERIC_DISPLAY,
  };

  static bool startScan(uint32_t seconds, bool duplicates);
  static bool stopScan(void);
  static bool startExplore(const char *address, PairMode mode, bool keepBond);
  static bool stopExplore(bool keepBond);
  static bool readExplore(void);
  static bool pairConfirm(bool accept);
  static bool pairKey(uint32_t key);
  static bool isExploreRunning(void);
};

}  // namespace Furble

#endif
