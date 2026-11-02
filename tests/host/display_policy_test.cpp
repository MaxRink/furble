#include <cstdint>
#include <cstdio>

#include "FurbleDisplayPolicy.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

}  // namespace

int main() {
  using Furble::DisplayPolicy::DIM_TO_OFF_MS;
  using Furble::DisplayPolicy::dimToOffDue;

  check(DIM_TO_OFF_MS == 30000, "the dim dwell is 30 s");
  check(!dimToOffDue(true, 1000, 1000), "the first dim tick is not due");
  check(!dimToOffDue(true, 30999, 1000), "29.999 s is not due");
  check(dimToOffDue(true, 31000, 1000), "30 s is due");
  check(dimToOffDue(true, 40000, 1000), "past the dwell is due");

  // Activity restores the panel and clears the dim flag in the UI; the helper
  // must not fire when the panel is not dimmed.
  check(!dimToOffDue(false, 40000, 1000), "an active panel is never due");

  // The millisecond tick wraps about every 49 days. Subtraction is modulo
  // 2^32, so the dwell still measures correctly across the wrap.
  check(dimToOffDue(true, 30000, 0xFFFFFFF0U), "a wrapped tick past the dwell is due");
  check(!dimToOffDue(true, 20000, 0xFFFFFFF0U), "a wrapped tick before the dwell is not due");

  if (failures != 0) {
    return 1;
  }
  std::printf("Display dim-to-off policy: PASS\n");
  return 0;
}
