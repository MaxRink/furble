#ifndef FURBLE_SIM_COMPANION_H
#define FURBLE_SIM_COMPANION_H

#include <cstdint>

namespace Furble {

/**
 * Simulator stand-in for the BLE companion service.
 *
 * The real Companion depends on NimBLE. The simulator has no BLE stack, so
 * this fake reports the companion as disabled with no pending pairing.
 */
class Companion {
 public:
  static Companion &getInstance(void) {
    static Companion instance;
    return instance;
  }

  bool isEnabled(void) const { return false; }
  bool hasPendingPairing(void) const { return false; }
  uint32_t getPendingPairingPin(void) const { return 0; }
  void confirmPairing(bool) {}
  void reloadSetting(bool) {}

 private:
  Companion() {}
};

}  // namespace Furble

#endif
