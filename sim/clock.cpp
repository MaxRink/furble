#include <atomic>

#include "clock.h"

namespace Furble::Sim {
namespace {

std::atomic<uint32_t> nowMillis {0};

}  // namespace

uint32_t clockMillis(void) {
  return nowMillis.load();
}

void advanceClock(uint32_t milliseconds) {
  nowMillis.fetch_add(milliseconds);
}

}  // namespace Furble::Sim
