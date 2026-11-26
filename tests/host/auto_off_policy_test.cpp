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

  // A refused power off holds the automatic paths off for its backoff, so a
  // wedged SD writer cannot stall the UI task on every one-second pass.
  using Furble::AutoOff::retryAfter;
  using Furble::AutoOff::retryBlocked;
  assert(retryBlocked(1000, retryAfter(1000)));
  assert(retryBlocked(1000 + Furble::AutoOff::POWER_OFF_RETRY_MS - 1, retryAfter(1000)));
  assert(!retryBlocked(1000 + Furble::AutoOff::POWER_OFF_RETRY_MS, retryAfter(1000)));
  // No refusal has been recorded yet, so the first attempt is not held off.
  assert(!retryBlocked(1000, 0));
  // A retry point past the 32-bit millisecond wrap still holds.
  const uint32_t wrapped = 0xFFFFFFFFu - 1000u;
  assert(retryBlocked(wrapped + 1u, retryAfter(wrapped)));
  assert(!retryBlocked(0u, wrapped));
  return 0;
}
