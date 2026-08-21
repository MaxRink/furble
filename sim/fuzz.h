#ifndef FURBLE_SIM_FUZZ_H
#define FURBLE_SIM_FUZZ_H

#include <cstdint>

namespace Furble {
class UI;
}

namespace Furble::Sim {

// Seeded UI fuzzer. Feeds a deterministic pseudo random stream of physical
// button presses, menu navigation, settings edits and page transitions into
// the real FurbleUI, then checks a set of invariants after every event. The
// same seed always produces the same event stream, so any finding reproduces
// exactly. The fuzzer is FURBLE_SIM only and never affects firmware builds.

// Enable the fuzzer with a seed and an event budget. verbose prints every
// event as it runs so a finding can be minimised by replaying to its step.
void fuzzConfigure(uint64_t seed, uint32_t steps, bool verbose);

// True once fuzzConfigure has armed the fuzzer.
bool fuzzActive(void);

// Advance the fuzzer by one simulator tick. Executes at most one event per call
// with settle ticks in between so LVGL can process the transition. Runs on the
// UI task, so LVGL reads stay single threaded. Exits the process when the event
// budget is spent, with a non-zero code if any hard invariant failed.
void fuzzTick(Furble::UI *ui);

}  // namespace Furble::Sim

#endif
