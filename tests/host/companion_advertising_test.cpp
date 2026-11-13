#include <cstdint>
#include <cstdio>

#include "FurbleCompanionAdvertising.h"

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
  using namespace Furble::CompanionAdvertising;

  check(FAST_INTERVAL == 160, "the fast interval is 100 ms in 0.625 ms units");
  check(SLOW_INTERVAL == 1636, "the slow interval is Apple's 1022.5 ms step");
  check(FAST_WINDOW_MS == 60000, "the fast window lasts 60 s");

  Cadence cadence;
  check(cadence.interval(0) == SLOW_INTERVAL, "an unboosted cadence advertises slowly");

  // Enable, boot, disconnect and button wake all open the window.
  cadence.boost(1000);
  check(cadence.interval(1000) == FAST_INTERVAL, "a boost advertises fast at once");
  check(cadence.interval(60999) == FAST_INTERVAL, "the window holds just before 60 s");
  check(cadence.interval(61000) == SLOW_INTERVAL, "the window closes after 60 s");

  // The service task restarts reconnect advertising on each edge only.
  check(!cadence.needsRestart(FAST_INTERVAL, 30000), "fast inside the window keeps running");
  check(cadence.needsRestart(FAST_INTERVAL, 61000), "fast after the window falls back");
  check(!cadence.needsRestart(SLOW_INTERVAL, 61000), "slow after the window keeps running");

  // A new boost while slow reopens the fast window and extends it from now.
  cadence.boost(90000);
  check(cadence.needsRestart(SLOW_INTERVAL, 90000), "a boost while slow restarts fast");
  check(cadence.interval(149999) == FAST_INTERVAL, "a later boost extends the window");
  check(cadence.interval(150000) == SLOW_INTERVAL, "the extended window still closes");

  // A boost inside the window extends it without forcing a restart.
  cadence.boost(120000);
  check(!cadence.needsRestart(FAST_INTERVAL, 120000), "a boost while fast needs no restart");
  check(cadence.interval(179999) == FAST_INTERVAL, "a boost while fast extends the window");

  if (failures != 0) {
    return 1;
  }
  std::printf("companion_advertising_test: PASS\n");
  return 0;
}
