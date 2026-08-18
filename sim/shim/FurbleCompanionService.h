// Guard matches the real include/FurbleCompanionService.h on purpose. If a TU
// pulls in both (shim first via the sim include path), the real header becomes
// a no-op and there is no duplicate Furble::CompanionService definition.
#ifndef FURBLE_COMPANION_SERVICE_H
#define FURBLE_COMPANION_SERVICE_H

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
