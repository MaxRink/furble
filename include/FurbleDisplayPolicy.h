#ifndef FURBLE_DISPLAY_POLICY_H
#define FURBLE_DISPLAY_POLICY_H

#include <cstdint>

namespace Furble {
namespace DisplayPolicy {

/**
 * How long the panel stays dimmed before a "Dim, then off" mode sleeps it.
 *
 * This mode is additive: the existing Dim and Off modes are unchanged. The
 * delay is measured from the moment the display entered DIM, not from the
 * inactivity timeout, so user activity that restores the panel also restarts
 * the dim dwell.
 */
constexpr uint32_t DIM_TO_OFF_MS = 30 * 1000;

/** True when a dimmed panel has stayed dim for the full dwell. Wrap-safe. */
constexpr bool dimToOffDue(bool dimmed, uint32_t now, uint32_t dimSince) {
  return dimmed && (static_cast<uint32_t>(now - dimSince) >= DIM_TO_OFF_MS);
}

}  // namespace DisplayPolicy
}  // namespace Furble

#endif  // FURBLE_DISPLAY_POLICY_H
