#ifndef FURBLE_SIM_COMPANION_H
#define FURBLE_SIM_COMPANION_H

#include <cstdint>

#include "driver.h"

namespace Furble {

/**
 * Simulator stand-in for the BLE companion transport.
 *
 * The real Companion depends on NimBLE. The simulator has no BLE stack, so
 * this fake forwards only the rig controls needed by the UI.
 */
class CompanionGatt {
 public:
  static CompanionGatt &getInstance(void) {
    static CompanionGatt instance;
    return instance;
  }

  bool isEnabled(void) const { return Sim::rigIsEnabled(); }
  bool hasPendingPairing(void) const { return Sim::rigHasPendingPairing(); }
  uint32_t getPendingPairingPin(void) const { return Sim::rigPendingPairingPin(); }
  void confirmPairing(bool accept) { Sim::rigConfirmPairing(accept); }
  void reloadSetting(bool pairingWindow) { Sim::rigReloadSetting(pairingWindow); }

 private:
  CompanionGatt() {}
};

using Companion = CompanionGatt;

}  // namespace Furble

#endif
