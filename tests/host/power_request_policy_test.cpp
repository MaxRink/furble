#include <cstdint>
#include <cstdio>

#include "FurblePowerPolicy.h"

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
  using Furble::PowerPolicy::makeRequest;
  using Furble::PowerPolicy::PmRequest;
  using Furble::PowerPolicy::requestLightSleep;

  check(requestLightSleep(true), "tickless builds may request light sleep");
  check(!requestLightSleep(false), "non-tickless builds must not request light sleep");

  // The production request keeps the frequency fields verbatim in both cases.
  const PmRequest tickless = makeRequest(160, 80, true);
  check(tickless.max_freq_mhz == 160, "the requested maximum frequency is kept");
  check(tickless.min_freq_mhz == 80, "the requested minimum frequency is kept");
  check(tickless.light_sleep_enable, "a tickless request asks for light sleep");

  // The Waveshare shape: CONFIG_PM_ENABLE for DFS, tickless idle off. ESP-IDF
  // rejects this combination when light_sleep_enable is true, so the request
  // must ask for DFS only.
  const PmRequest waveshare = makeRequest(160, 80, false);
  check(waveshare.max_freq_mhz == 160, "a DFS-only request keeps the maximum frequency");
  check(waveshare.min_freq_mhz == 80, "a DFS-only request keeps the minimum frequency");
  check(!waveshare.light_sleep_enable, "a DFS-only request must not ask for light sleep");

  // A 240 MHz setting on a tickless board is still a valid light-sleep request.
  const PmRequest fast = makeRequest(240, 80, true);
  check(fast.max_freq_mhz == 240 && fast.light_sleep_enable, "240 MHz tickless is preserved");

  if (failures != 0) {
    return 1;
  }
  std::printf("Power request policy: PASS\n");
  return 0;
}
