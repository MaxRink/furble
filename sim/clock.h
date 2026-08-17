#ifndef FURBLE_SIM_CLOCK_H
#define FURBLE_SIM_CLOCK_H

#include <cstdint>

namespace Furble::Sim {

uint32_t clockMillis(void);
void advanceClock(uint32_t milliseconds);

}  // namespace Furble::Sim

#endif
