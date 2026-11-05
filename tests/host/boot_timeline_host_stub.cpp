#include <cstdint>

// Host tests that link the production boot timeline only need it to compile and
// record marks. Their focus is the surrounding subsystem, not boot timing.
extern "C" uint32_t furbleBootTimelineHostResetMs(void) {
  return 0;
}

extern "C" int64_t furbleBootTimelineHostNowUs(void) {
  return 0;
}
