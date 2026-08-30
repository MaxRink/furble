#include <cstdint>
#include <limits>

#include "connect_model.h"

using Furble::Sim::connectDeadlineReached;

static_assert(!connectDeadlineReached(749, 0));
static_assert(connectDeadlineReached(750, 0));

int main() {
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 100;
  return !connectDeadlineReached(start + 750, start)
             || connectDeadlineReached(start + 749, start)
         ? 1
         : 0;
}
