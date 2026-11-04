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

  if (!ok) {
    return 1;
  }
  std::printf("gps fix freshness: PASS\n");
  return 0;
}
