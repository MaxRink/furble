// Host checks for the companion first-enable admission gate.

#include <cstdio>

#include "FurbleCompanionMemory.h"

using namespace Furble::CompanionMemory;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

}  // namespace

int main() {
  static_assert(START_INTERNAL_FREE_FLOOR == 8320, "companion free floor drifted");
  static_assert(START_INTERNAL_LARGEST_FLOOR == 4224, "companion largest floor drifted");
  static_assert(START_INTERNAL_LARGEST_FLOOR > TASK_STACK_BYTES,
                "the largest block must hold the task stack and its overhead");

  check(startGate(START_INTERNAL_FREE_FLOOR, START_INTERNAL_LARGEST_FLOOR).open,
        "the exact companion floor was rejected");
  check(!startGate(START_INTERNAL_FREE_FLOOR - 1, START_INTERNAL_LARGEST_FLOOR).open,
        "the companion started one byte below the free floor");
  check(!startGate(START_INTERNAL_FREE_FLOOR, START_INTERNAL_LARGEST_FLOOR - 1).open,
        "the companion started below the largest-block floor");

  const gate_t refused = startGate(6871, 5108);
  check(!refused.open && refused.freeBytes == 6871 && refused.largestBlock == 5108
            && refused.freeFloor == START_INTERNAL_FREE_FLOOR
            && refused.largestFloor == START_INTERNAL_LARGEST_FLOOR,
        "a refusal does not carry the measured pair and the floors for the log");
  // The pre-#394 measured StickS3 pair with the WebUI off still admits it.
  check(startGate(13543, 12788).open, "the measured pre-#394 StickS3 pair was refused");

  if (failures != 0) {
    return 1;
  }
  std::printf("companion_memory_test: PASS\n");
  return 0;
}
