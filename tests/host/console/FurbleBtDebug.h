// Host BtDebug shim for the console command suite.
//
// src/FurbleBtDebug.cpp is a NimBLE HCI scan and explore dumper with no host
// model, so the double stands in at the boundary. Every entry point records
// its arguments and returns a test controlled result, which is how the 'bt'
// command tree is checked all the way through parsing and dispatch.
#ifndef FURBLE_BT_DEBUG_H
#define FURBLE_BT_DEBUG_H

#include <cstdint>

namespace Furble {

class BtDebug {
 public:
  enum class PairMode : uint8_t {
    NONE,
    JUST_WORKS,
    NUMERIC_DISPLAY,
  };

  static bool startScan(uint32_t seconds, bool duplicates);
  static bool stopScan(void);
  static bool startExplore(const char *address, PairMode mode, bool keep);
  static bool stopExplore(bool keep);
  static bool readExplore(void);
  static bool pairConfirm(bool accept);
  static bool pairKey(uint32_t key);
  static bool isExploreRunning(void);
};

}  // namespace Furble

#endif
