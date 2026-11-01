#include <cstdint>
#include <cstdio>

#include "FurblePlatformPolicy.h"

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
  using Furble::PlatformPolicy::PM1_POLL_MS;
  using Furble::PlatformPolicy::pm1PollDue;

  check(PM1_POLL_MS == 20, "the PM1 poll interval is 20 ms");
  check(pm1PollDue(1000, 980), "exactly 20 ms is due");
  check(!pm1PollDue(1000, 981), "19 ms is not due");
  check(pm1PollDue(1005, 980), "over the interval is due");
  // A millisecond tick wraps about every 49 days. The subtraction is modulo
  // 2^32, so a wrapped now still measures the elapsed interval correctly.
  check(pm1PollDue(10, 0xFFFFFFF0U), "a wrapped tick is due after the interval");
  check(!pm1PollDue(0, 0xFFFFFFF0U), "a wrapped tick before the interval is not due");

  if (failures != 0) {
    return 1;
  }
  std::printf("PM1 poll policy: PASS\n");
  return 0;
}
