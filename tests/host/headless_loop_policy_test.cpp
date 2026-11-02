#include <cstdint>
#include <cstdio>

#include "FurbleHeadlessPolicy.h"

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
  using Furble::HeadlessPolicy::LOOP_MS;
  using Furble::HeadlessPolicy::loopSleepMs;
  using Furble::HeadlessPolicy::MIN_SLEEP_MS;

  check(LOOP_MS == 50, "the headless idle period is 50 ms");
  check(MIN_SLEEP_MS == 1, "a shortened sleep never goes below one tick unit");

  // A GPS deadline a full second away is paced by the loop period: about 20
  // wakeups per second instead of the old 200.
  check(loopSleepMs(0, 1000000) == LOOP_MS, "a distant GPS deadline sleeps one loop period");
  check((1000000 / (LOOP_MS * 1000)) == 20, "one second holds twenty loop periods");

  // A deadline inside the loop period shortens the sleep and keeps the 1 Hz
  // service on time.
  check(loopSleepMs(0, 20000) == 20, "a nearer deadline shortens the sleep");
  check(loopSleepMs(980000, 1000000) == 20, "a deadline 20 ms out sleeps 20 ms");
  check(loopSleepMs(999000, 1000000) == 1, "a deadline under 1 ms sleeps the floor");
  check(loopSleepMs(999500, 1000000) == MIN_SLEEP_MS, "a sub-millisecond remainder uses the floor");

  // Exactly one loop period out still uses the full period.
  check(loopSleepMs(0, 50000) == LOOP_MS, "exactly one loop period is a full sleep");

  // Due and overdue deadlines return zero so the caller services GPS and
  // recomputes; the next iteration is then paced again.
  check(loopSleepMs(1000000, 1000000) == 0, "a due deadline does not sleep");
  check(loopSleepMs(1000001, 1000000) == 0, "an overdue deadline does not sleep");

  if (failures != 0) {
    return 1;
  }
  std::printf("Headless loop policy: PASS\n");
  return 0;
}
