#include <cstdint>

#include "esp_timer.h"

// The simulator clock starts at zero. The boot timeline is observability only
// here; scenario timings are virtual and are not hardware measurements.
extern "C" uint32_t furbleBootTimelineHostResetMs(void) {
  return 0;
}

extern "C" int64_t furbleBootTimelineHostNowUs(void) {
  return esp_timer_get_time();
}
