#ifndef FURBLE_SIM_MEMORY_PROFILE_H
#define FURBLE_SIM_MEMORY_PROFILE_H

#include <cstddef>
#include <string>

namespace Furble::Sim {

/**
 * Static and dynamic memory profiling for the simulator.
 *
 * The static table is a per-profile budget compiled from sdkconfig values and
 * production constants. The dynamic side is the heap model's tagged allocation
 * stream: owner totals, watermark, fragmentation, timeline and leaks.
 */
void memoryProfileTick(void);

bool memoryDump(const std::string &path, const std::string &scenario);

std::string memoryQuery(const std::string &key);

/** Scheduler task stacks, as name:requested[:psram] entries. */
std::string schedulerTaskReport(void);

}  // namespace Furble::Sim

#endif
