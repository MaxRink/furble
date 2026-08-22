// Board-conditional text size policy checks for include/FurbleTextSize.h.
//
// The policy decides the fresh-device default, the highest selectable size and
// the render-time clamp, all at compile time from the board macro. This suite
// is compiled twice: once with no board macro (the large-board default) and
// once with FURBLE_M5STICKC (the 80x160 small board), so both branches of the
// policy are proven. The checks are static_asserts, so a wrong policy fails to
// compile; main only reports the run so ctest sees a passing binary.

#include <iostream>

#include "FurbleTextSize.h"

namespace TS = Furble::TextSizePolicy;

#if defined(FURBLE_M5STICKC)

// The 80x160 M5StickC cannot fit Large, so it caps at Normal and defaults a
// fresh device to Small.
static_assert(TS::DEFAULT == TS::SMALL, "M5StickC defaults to Small");
static_assert(TS::MAX == TS::NORMAL, "M5StickC caps at Normal");
static_assert(TS::COUNT == 2, "M5StickC offers two sizes");
static_assert(TS::clamp(TS::LARGE) == TS::NORMAL, "a stored Large clamps to Normal on M5StickC");
static_assert(TS::clamp(TS::NORMAL) == TS::NORMAL, "Normal is unchanged on M5StickC");
static_assert(TS::clamp(TS::SMALL) == TS::SMALL, "Small is unchanged on M5StickC");
static_assert(TS::clamp(200) == TS::NORMAL, "an out-of-range value clamps to Normal on M5StickC");

const char *kBoard = "M5StickC (80x160)";

#else

// Every other board keeps all three sizes and the Normal default.
static_assert(TS::DEFAULT == TS::NORMAL, "large boards default to Normal");
static_assert(TS::MAX == TS::LARGE, "large boards keep Large");
static_assert(TS::COUNT == 3, "large boards offer three sizes");
static_assert(TS::clamp(TS::LARGE) == TS::LARGE, "Large is unchanged on large boards");
static_assert(TS::clamp(200) == TS::LARGE, "an out-of-range value clamps to Large on large boards");

const char *kBoard = "large board";

#endif

// The policy is only ever meaningful if a fresh device lands on a valid size.
static_assert(TS::DEFAULT <= TS::MAX, "the default must be within the selectable range");

int main() {
  std::cout << "text size policy tests: PASS (" << kBoard << ")\n";
  return 0;
}
