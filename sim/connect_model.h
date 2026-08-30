#ifndef FURBLE_SIM_CONNECT_MODEL_H
#define FURBLE_SIM_CONNECT_MODEL_H

#include <cstdint>

#include "clock.h"

namespace Furble::Sim {

// Explicitly synthetic UNCERTIFIED simulator model. The control task owns the
// start timestamp; this shared predicate keeps its 750 ms boundary testable
// without making state observation responsible for progression.
constexpr uint32_t CONNECT_DURATION_MS = 750;

constexpr bool connectDeadlineReached(uint32_t now, uint32_t start) {
  return clockElapsed(now, start) >= CONNECT_DURATION_MS;
}

}  // namespace Furble::Sim

#endif
