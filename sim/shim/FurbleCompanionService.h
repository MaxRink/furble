#ifndef FURBLE_SIM_COMPANION_SERVICE_H
#define FURBLE_SIM_COMPANION_SERVICE_H

#include <cstdint>

namespace Furble {

// The simulator's UI never includes the BLE service header. Keep a
// no-capability shadow for any UI-only simulator source that does.
class CompanionService {
 public:
  void init(void) {}
  void deinit(void) {}
  void beginPairing(uint32_t) {}
  bool hasPendingPairing(void) const { return false; }
  uint32_t getPendingPairingPin(void) const { return 0; }
  void confirmPairing(bool) {}
};

}  // namespace Furble

#endif
