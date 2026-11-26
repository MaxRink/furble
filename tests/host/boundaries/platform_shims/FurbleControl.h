#ifndef FURBLE_PLATFORM_SHIM_CONTROL_H
#define FURBLE_PLATFORM_SHIM_CONTROL_H

#include <cstdint>

namespace Furble {
// Control double: the platform touches the camera lifecycle only in
// prepareRestart(), which no rail assertion drives.
class Control {
 public:
  static constexpr uint32_t DISCONNECT_TIMEOUT_MS = 1000;
  static Control &getInstance(void);
  bool disconnect(uint32_t timeout_ms, bool for_restart);
};
}  // namespace Furble

#endif
