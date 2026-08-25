#include <cstdint>
#include <limits>

#include "FurbleTime.h"
#include "M5PM1.h"
#include "clock.h"

using Furble::Sim::clockDeadlineReached;
using Furble::Sim::clockElapsed;

static_assert(clockElapsed(4, std::numeric_limits<uint32_t>::max() - 1) == 6);
static_assert(!clockDeadlineReached(std::numeric_limits<uint32_t>::max() - 1, 4));
static_assert(!clockDeadlineReached(3, 4));
static_assert(clockDeadlineReached(4, 4));
static_assert(clockDeadlineReached(0, std::numeric_limits<uint32_t>::max()));
static_assert(Furble::Time::elapsed(4, std::numeric_limits<uint32_t>::max() - 1) == 6);
static_assert(Furble::Time::deadlineReached(4, 4));

int main() {
  using namespace Furble::Sim;
  if (clockElapsed(0, std::numeric_limits<uint32_t>::max()) != 1
      || !clockDeadlineReached(0, std::numeric_limits<uint32_t>::max())) {
    return 1;
  }

  setClockMillis(std::numeric_limits<uint32_t>::max() - 5000);
  M5PM1 pm1;
  if (pm1.begin(nullptr) != M5PM1_OK || pm1.wdtSet(10) != M5PM1_ERROR
      || pm1.wdtSet(10) != M5PM1_OK) {
    return 1;
  }
  setClockMillis(4998);
  if (pm1.watchdogExpired()) {
    return 1;
  }
  setClockMillis(4999);
  return pm1.watchdogExpired() ? 0 : 1;
}
