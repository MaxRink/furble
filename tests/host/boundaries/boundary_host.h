#ifndef FURBLE_BOUNDARY_HOST_H
#define FURBLE_BOUNDARY_HOST_H

#include <cstdint>
#include <string>

namespace BoundaryHost {

void serviceTimers();
uint32_t timerFireCount();
void advanceTick(uint32_t ms);
uint32_t tick();
uint32_t wakeCalls();
uint32_t psramAllocations();
int gpioDirection(int pin);
int gpioLevel(int pin);
bool logContains(const char *needle);

}  // namespace BoundaryHost

#endif
