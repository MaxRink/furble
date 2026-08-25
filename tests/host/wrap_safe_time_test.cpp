#include <cstdint>
#include <iostream>
#include <limits>

#include "WrapSafeTime.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

}  // namespace

int main() {
  using Furble::Host::timeoutPending;
  constexpr uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 2U;

  check(timeoutPending(100U, 104U, 5U), "ordinary interval remains pending");
  check(!timeoutPending(100U, 105U, 5U), "ordinary interval expires at its bound");
  check(timeoutPending(nearWrap, 0U, 5U), "interval remains pending across wrap");
  check(timeoutPending(nearWrap, 1U, 5U), "last millisecond before wrapped bound is pending");
  check(!timeoutPending(nearWrap, 2U, 5U), "wrapped interval expires at its bound");
  check(!timeoutPending(nearWrap, 3U, 5U), "wrapped interval stays expired");
  check(!timeoutPending(42U, 42U, 0U), "zero timeout is immediately expired");

  return failures == 0 ? 0 : 1;
}
