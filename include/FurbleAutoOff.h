#ifndef FURBLE_AUTO_OFF_H
#define FURBLE_AUTO_OFF_H

#include <cstdint>

namespace Furble::AutoOff {

/** Return true when the disconnected idle auto-off policy may run. */
constexpr bool shouldPowerOff(uint8_t minutes,
                              bool disconnected,
                              bool scanActive,
                              uint32_t inactiveMilliseconds,
                              bool charging,
                              bool allowWhileCharging) {
  if ((minutes == 0) || !disconnected || scanActive || (charging && !allowWhileCharging)) {
    return false;
  }
  return inactiveMilliseconds >= (static_cast<uint32_t>(minutes) * 60000U);
}

}  // namespace Furble::AutoOff

#endif
