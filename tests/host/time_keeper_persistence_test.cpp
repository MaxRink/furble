#include <cstdint>
#include <cstdio>

#include "FurbleTimeKeeper.h"
#include "M5Unified.h"
#include "nvs.h"

namespace {

int64_t now_us = 0;

void setTimeMs(uint64_t milliseconds) {
  now_us = static_cast<int64_t>(milliseconds * 1000ULL);
}

void bootWithoutRtc(void) {
  M5.board = m5::board_t::board_M5StickS3;
  M5.Rtc.enabled = false;
  Furble::TimeKeeper::getInstance().resetForTest();
  Furble::TimeKeeper::init();
}

int check(bool condition, const char *message, int line) {
  if (!condition) {
    std::fprintf(stderr, "check failed at %d: %s\n", line, message);
    return 1;
  }
  return 0;
}

#define CHECK(condition)                               \
  do {                                                 \
    if (check((condition), #condition, __LINE__) != 0) \
      return 1;                                        \
  } while (false)

}  // namespace

extern "C" int64_t esp_timer_get_time(void) {
  return now_us;
}

int main() {
  constexpr uint64_t epoch = 1704110400000000ULL;
  nvs_test_reset();
  setTimeMs(0);
  bootWithoutRtc();

  auto &keeper = Furble::TimeKeeper::getInstance();
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  CHECK(nvs_test_commit_count() == 1);
  CHECK(nvs_test_value_type("furble", "time_state") == NVS_TEST_BLOB);

  // A clean shutdown shortly after synchronization does not rewrite the same
  // durable state. The no-RTC checkpoint waits for a real runtime age.
  setTimeMs(60ULL * 60ULL * 1000ULL);
  keeper.flush();
  keeper.flush();
  CHECK(nvs_test_commit_count() == 1);

  setTimeMs(3ULL * 60ULL * 60ULL * 1000ULL);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 2);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 2);

  // Reset the service without erasing NVS. A reboot starts a new age budget,
  // so an immediate shutdown cannot repeatedly burn the same flash sector.
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(keeper.status().valid);
  CHECK(keeper.status().source == Furble::TimeSource::NVS);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 2);
  setTimeMs(3ULL * 60ULL * 60ULL * 1000ULL);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 3);

  // A correction storm can ask to persist on every update, but normal writes
  // stay at the six-hour interval. The loop spans one day and must stay under
  // the four normal commits plus the initial commit.
  nvs_test_reset();
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  for (uint64_t hour = 1; hour <= 24; hour++) {
    setTimeMs(hour * 60ULL * 60ULL * 1000ULL);
    CHECK(keeper.update(Furble::TimeSource::GPS,
                        epoch + hour * 60ULL * 60ULL * 1000000ULL + 120000000ULL, 1000));
  }
  CHECK(nvs_test_commit_count() <= 5);

  // Alternating a correction with a graceful shutdown can use the shorter
  // checkpoint interval, but still cannot exceed eight commits in a day.
  nvs_test_reset();
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  for (uint64_t slot = 1; slot <= 7; slot++) {
    setTimeMs(slot * Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
    CHECK(keeper.update(
        Furble::TimeSource::GPS,
        epoch + slot * Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS * 1000ULL
            + 120000000ULL,
        1000));
    keeper.flush();
  }
  CHECK(nvs_test_commit_count() <= 8);

  // The blob remains one versioned record. A second service instance can
  // restore it through the real Preferences wrapper and continue its budget.
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(keeper.status().source == Furble::TimeSource::NVS);
  CHECK(keeper.status().nvs_write_count == 0);
  CHECK(nvs_test_commit_count() <= 8);
  return 0;
}
