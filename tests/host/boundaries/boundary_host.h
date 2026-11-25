#ifndef FURBLE_BOUNDARY_HOST_H
#define FURBLE_BOUNDARY_HOST_H

#include <cstdint>
#include <string>
#include <vector>

namespace BoundaryHost {

/**
 * A GPIO level change with the host virtual microsecond clock.
 *
 * Only esp_rom_delay_us() advances that clock, so a requested busy-wait shows
 * up as the interval between two edges. It is not wall-clock time and does not
 * prove the device waited, only that production asked the ROM for the delay.
 */
struct GpioEdge {
  int level;
  uint64_t micros;
};

void serviceTimers();
uint32_t timerFireCount();
void advanceTick(uint32_t ms);
uint32_t tick();
uint32_t wakeCalls();
uint32_t psramAllocations();
int gpioDirection(int pin);
int gpioLevel(int pin);
std::vector<GpioEdge> gpioEdges(int pin);
bool logContains(const char *needle);

}  // namespace BoundaryHost

#endif
