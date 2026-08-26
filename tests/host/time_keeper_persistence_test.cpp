#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "FurbleTimeKeeper.h"
#include "M5Unified.h"
#include "Preferences.h"
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

void bootWithUnbackedRtc(void) {
  M5.board = m5::board_t::board_M5StickS3;
  M5.Rtc.enabled = true;
  M5.Rtc.voltageLow = false;
  M5.Rtc.value.date = {2024, 1, 1};
  M5.Rtc.value.time = {12, 0, 0};
  Furble::TimeKeeper::getInstance().resetForTest();
  Furble::TimeKeeper::init();
}

bool readDurable(Furble::TimeSample &sample) {
  Furble::Preferences preferences;
  if (!preferences.begin("furble", true) || !preferences.isKey("time_state")) {
    preferences.end();
    return false;
  }
  const size_t length = preferences.getBytesLength("time_state");
  std::array<uint8_t, 64> buffer = {};
  const bool read =
      length <= buffer.size() && preferences.get("time_state", buffer.data(), length) == length;
  preferences.end();
  return read && Furble::TimeKeeperPolicy::decode(buffer.data(), length, sample);
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
  CHECK(nvs_test_commit_count() == 1);
  setTimeMs(Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 2);
  Furble::TimeSample durable = {};
  CHECK(readDurable(durable));
  CHECK(durable.epoch_us
        == epoch + Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS * 1000ULL);
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
  setTimeMs(Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
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
  std::vector<uint64_t> commitTimes = {0};
  for (uint64_t slot = 1; slot <= 6; slot++) {
    setTimeMs(slot * Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
    const size_t before = nvs_test_commit_count();
    CHECK(keeper.update(
        Furble::TimeSource::GPS,
        epoch + slot * Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS * 1000ULL
            + 120000000ULL,
        1000));
    keeper.flush();
    if (nvs_test_commit_count() != before) {
      commitTimes.push_back(now_us / 1000);
    }
  }
  CHECK(nvs_test_commit_count() == 7);

  // The exact 24-hour boundary is intentionally not eligible. A later
  // checkpoint is allowed, and every inclusive rolling window stays bounded.
  setTimeMs(24ULL * 60ULL * 60ULL * 1000ULL);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 7);
  setTimeMs(24ULL * 60ULL * 60ULL * 1000ULL
            + Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 8);
  commitTimes.push_back(now_us / 1000);
  for (size_t first = 0; first < commitTimes.size(); first++) {
    size_t inWindow = 0;
    for (size_t current = first; current < commitTimes.size(); current++) {
      if (commitTimes[current] - commitTimes[first] <= 24ULL * 60ULL * 60ULL * 1000ULL) {
        inWindow++;
      }
    }
    CHECK(inWindow <= 8);
  }

  // RTC availability alone does not imply retention across power loss. An
  // unbacked calendar RTC still needs the same age-qualified NVS checkpoints.
  // The old policy wrote the first record, then stopped forever because it
  // checked availability instead of the backed capability.
  nvs_test_reset();
  setTimeMs(0);
  bootWithUnbackedRtc();
  CHECK(keeper.status().rtc_available);
  CHECK(!keeper.status().rtc_battery_backed);
  setTimeMs(Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 1);
  setTimeMs(2 * Furble::TimeKeeperPolicy::NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS);
  keeper.flush();
  CHECK(nvs_test_commit_count() == 2);

  // A failed setter never reaches durable storage. A simulated reboot must
  // therefore come up without a time record.
  nvs_test_reset();
  setTimeMs(0);
  bootWithoutRtc();
  nvs_test_fail_set_on(1);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  CHECK(nvs_test_commit_count() == 0);
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(!keeper.status().valid);

  // A power cut during commit discards staged data as the Preferences handle
  // closes. The next boot sees no partial time blob, then a retry commits it.
  nvs_test_reset();
  setTimeMs(0);
  bootWithoutRtc();
  nvs_test_fail_commit_on(1);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  CHECK(nvs_test_commit_count() == 0);
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(!keeper.status().valid);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  CHECK(nvs_test_commit_count() == 1);

  // The blob remains one versioned record. A second service instance can
  // restore it through the real Preferences wrapper and continue its budget.
  setTimeMs(0);
  bootWithoutRtc();
  CHECK(keeper.status().source == Furble::TimeSource::NVS);
  CHECK(keeper.status().nvs_write_count == 0);
  CHECK(nvs_test_commit_count() <= 8);
  return 0;
}
