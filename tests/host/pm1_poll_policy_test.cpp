#include <cstdint>
#include <cstdio>

#include "FurblePlatformPolicy.h"

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
  using Furble::PlatformPolicy::PM1_POLL_MS;
  using Furble::PlatformPolicy::pm1PollDue;

  check(PM1_POLL_MS == 20, "the PM1 poll interval is 20 ms");
  check(pm1PollDue(1000, 980), "exactly 20 ms is due");
  check(!pm1PollDue(1000, 981), "19 ms is not due");
  check(pm1PollDue(1005, 980), "over the interval is due");
  // A millisecond tick wraps about every 49 days. The subtraction is modulo
  // 2^32, so a wrapped now still measures the elapsed interval correctly.
  check(pm1PollDue(10, 0xFFFFFFF0U), "a wrapped tick is due after the interval");
  check(!pm1PollDue(0, 0xFFFFFFF0U), "a wrapped tick before the interval is not due");

  using Furble::PlatformPolicy::AXP192_ADC_ENABLE_1;
  using Furble::PlatformPolicy::AXP192_ADC_ENABLE_2;
  using Furble::PlatformPolicy::axp192AdcMaskKeepsRequired;

  check(axp192AdcMaskKeepsRequired(AXP192_ADC_ENABLE_1),
        "the AXP192 mask keeps battery voltage/current and VBUS voltage");
  check((AXP192_ADC_ENABLE_1 & 0x20) == 0, "the ACIN voltage channel is off");
  check((AXP192_ADC_ENABLE_1 & 0x10) == 0, "the ACIN current channel is off");
  check((AXP192_ADC_ENABLE_1 & 0x04) != 0,
        "the VBUS current channel stays on for M5Unified setExtOutput");
  check((AXP192_ADC_ENABLE_1 & 0x02) == 0, "the APS voltage channel is off");
  check((AXP192_ADC_ENABLE_1 & 0x01) == 0, "the TS pin channel is off");
  check(AXP192_ADC_ENABLE_2 == 0x00, "the temperature and GPIO channels are off");
  check(!axp192AdcMaskKeepsRequired(0x08), "a mask without the battery channels fails");
  check(!axp192AdcMaskKeepsRequired(0xC8), "a mask without VBUS current fails");

  if (failures != 0) {
    return 1;
  }
  std::printf("PM1 poll policy: PASS\n");
  return 0;
}
