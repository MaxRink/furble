// Canon Smart GPS timestamp regression.
//
// Camera Connect sends an absolute UTC epoch. The helper must not depend on
// the runtime timezone.

#include <cstdint>
#include <cstdio>

#include "protocol/CanonTime.h"

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
  using Furble::CanonProtocol::utcEpoch;

  check(utcEpoch(1970, 1, 1, 0, 0, 0) == 0, "the Unix epoch is zero");
  check(utcEpoch(2026, 11, 1, 0, 0, 0) == 1793491200U,
        "2026-11-01 00:00:00 UTC matches the absolute epoch");
  check(utcEpoch(2026, 11, 1, 12, 34, 56) == 1793536496U,
        "2026-11-01 12:34:56 UTC matches the absolute epoch");
  check(utcEpoch(2024, 2, 29, 0, 0, 0) != 0, "a leap day is accepted");
  check(utcEpoch(2025, 2, 29, 0, 0, 0) == 0, "a non-leap 29 February is rejected");
  check(utcEpoch(2026, 13, 1, 0, 0, 0) == 0, "a month above 12 is rejected");
  check(utcEpoch(2026, 1, 1, 24, 0, 0) == 0, "an hour above 23 is rejected");

  if (failures != 0) {
    return 1;
  }
  std::printf("Canon UTC epoch tests passed\n");
  return 0;
}
