#include <cstdint>
#include <cstdio>
#include <cstring>

#include "FurbleBootTimeline.h"

namespace {

int64_t g_nowUs = 0;

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

}  // namespace

extern "C" uint32_t furbleBootTimelineHostResetMs(void) {
  return 2310;
}

extern "C" int64_t furbleBootTimelineHostNowUs(void) {
  return g_nowUs;
}

int main() {
  using Furble::BootTimeline;

  check(BootTimeline::at(BootTimeline::APP_MAIN) == -1, "an unstarted timeline has no mark");
  BootTimeline::begin();
  check(BootTimeline::recorded(BootTimeline::APP_MAIN), "begin records app_main");
  check(BootTimeline::at(BootTimeline::APP_MAIN) == 0, "app_main is the microsecond origin");
  check(BootTimeline::resetToAppMs() == 2310, "the cycle-counter reset value is kept");
  check(BootTimeline::cycleCountToMs(240000000U, 240U) == 1000,
        "cycle counts convert to milliseconds");
  check(BootTimeline::cycleCountToMs(123456U, 240U) == 0,
        "sub-millisecond cycle counts round down");
  check(BootTimeline::cycleCountToMs(123456U, 0U) == 0, "a zero tick rate does not divide by zero");

  g_nowUs = 12000;
  BootTimeline::mark(BootTimeline::SETTINGS_DONE);
  check(BootTimeline::at(BootTimeline::SETTINGS_DONE) == 12000, "settings mark is kept");

  // A later call must not move an existing mark.
  g_nowUs = 900000;
  BootTimeline::mark(BootTimeline::SETTINGS_DONE);
  check(BootTimeline::at(BootTimeline::SETTINGS_DONE) == 12000, "the first mark wins");

  g_nowUs = 400000;
  BootTimeline::mark(BootTimeline::WIFI_ASSOC);
  g_nowUs = 500000;
  BootTimeline::mark(BootTimeline::PLATFORM_DONE);
  check(BootTimeline::at(BootTimeline::PLATFORM_DONE) == 500000, "platform mark is kept");
  check(!BootTimeline::recorded(BootTimeline::WIFI_IP), "unset marks stay unset");
  check(BootTimeline::at(BootTimeline::WIFI_IP) == -1, "an unset mark reports -1");

  char summary[512] = {};
  const size_t length = BootTimeline::summary(summary, sizeof(summary));
  check(length > 0, "the summary is not empty");
  check(std::strstr(summary, "app_main=0") != nullptr, "summary carries app_main");
  check(std::strstr(summary, "settings_done=12") != nullptr, "summary rounds to milliseconds");
  check(std::strstr(summary, "wifi_assoc=400") != nullptr, "summary keeps async marks");
  check(std::strstr(summary, "platform_done=500") != nullptr, "summary keeps platform");
  check(std::strstr(summary, "wifi_ip") == nullptr, "summary omits unset marks");

  // A smaller buffer is truncated safely and stays terminated.
  char tiny[24] = {};
  (void)BootTimeline::summary(tiny, sizeof(tiny));
  check(tiny[sizeof(tiny) - 1] == '\0', "a short summary buffer stays terminated");

  BootTimeline::logSummary();
  BootTimeline::logSummary();

  if (failures != 0) {
    return 1;
  }
  std::printf("Boot timeline: PASS\n");
  return 0;
}
