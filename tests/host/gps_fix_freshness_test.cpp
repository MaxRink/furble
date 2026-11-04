#include <cstdint>
#include <cstdio>

#include "FurbleGPSFreshness.h"

int main() {
  using Furble::GPSFreshness::fixIsCurrent;
  constexpr uint32_t kMaxAge = 30000;

  bool ok = true;
  if (!fixIsCurrent(true, true, 0, kMaxAge)) {
    std::fprintf(stderr, "a fresh valid fix was rejected\n");
    ok = false;
  }
  if (!fixIsCurrent(true, true, kMaxAge - 1, kMaxAge)) {
    std::fprintf(stderr, "a fix just inside the bound was rejected\n");
    ok = false;
  }
  if (fixIsCurrent(true, true, kMaxAge, kMaxAge)) {
    std::fprintf(stderr, "a fix at the bound was accepted\n");
    ok = false;
  }
  if (fixIsCurrent(true, true, kMaxAge + 1, kMaxAge)) {
    std::fprintf(stderr, "a stale fix was accepted\n");
    ok = false;
  }
  if (fixIsCurrent(false, true, 0, kMaxAge) || fixIsCurrent(true, false, 0, kMaxAge)) {
    std::fprintf(stderr, "an invalid position or quality was accepted\n");
    ok = false;
  }

  // The production snapshot uses evaluate(), so a single age read must drive
  // both the fix verdict and the reported age. A regression back to two
  // gps.location.age() calls fails the call count.
  int ageReads = 0;
  const auto fresh = Furble::GPSFreshness::evaluate(
      true, true,
      [&ageReads]() {
        ageReads++;
        return kMaxAge - 1;
      },
      kMaxAge);
  if ((ageReads != 1) || !fresh.fix || (fresh.locationAgeMs != kMaxAge - 1)) {
    std::fprintf(stderr, "a fresh snapshot did not use exactly one age read\n");
    ok = false;
  }
  ageReads = 0;
  const auto stale = Furble::GPSFreshness::evaluate(
      true, true,
      [&ageReads]() {
        ageReads++;
        return kMaxAge;
      },
      kMaxAge);
  if ((ageReads != 1) || stale.fix || (stale.locationAgeMs != kMaxAge)) {
    std::fprintf(stderr, "a stale snapshot did not use exactly one age read\n");
    ok = false;
  }

  if (!ok) {
    return 1;
  }
  std::printf("gps fix freshness: PASS\n");
  return 0;
}
