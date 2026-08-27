#include <cassert>
#include <cstdint>

#include "FurbleAutoOff.h"

int main() {
  using Furble::AutoOff::shouldPowerOff;

  assert(shouldPowerOff(1, true, false, 60000, false, false));
  // A board without charging telemetry supplies no charging sample; that
  // unknown capability must not disable normal disconnected auto-off.
  const bool chargingCapability = false;
  const bool chargingSample = false;
  assert(shouldPowerOff(1, true, false, 60000, chargingCapability && chargingSample, false));
  assert(!shouldPowerOff(1, true, false, 59999, false, false));
  assert(!shouldPowerOff(1, false, false, 60000, false, false));
  assert(!shouldPowerOff(1, true, true, 60000, false, false));

  // Charging is a hard guard by default, with an explicit opt-in for users
  // who want auto-off to remain active on external power.
  assert(!shouldPowerOff(1, true, false, 60000, true, false));
  assert(shouldPowerOff(1, true, false, 60000, true, true));
  return 0;
}
