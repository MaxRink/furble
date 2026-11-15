#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "FurbleTimeKeeper.h"
#include "M5Unified.h"
#include "Preferences.h"
#include "esp_system.h"
#include "nvs.h"

namespace {

int64_t now_us = 0;
// Model of the ESP system clock. The RTC timer keeps it running across a soft
// reset while esp_timer restarts at zero. A power-on loses it: it then reads
// only the time since boot, which is far behind any valid epoch.
bool system_set = false;
uint64_t system_base_us = 0;

void softResetClock(void) {
  system_base_us += static_cast<uint64_t>(now_us);
  now_us = 0;
}

void powerOnClock(void) {
  system_set = false;
  now_us = 0;
}

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

extern "C" uint64_t furble_sim_system_time_us(void) {
  return static_cast<uint64_t>(now_us) + (system_set ? system_base_us : 0);
}

extern "C" void furble_sim_set_system_time_us(uint64_t epoch_us) {
  system_base_us = epoch_us - static_cast<uint64_t>(now_us);
  system_set = true;
}

namespace {

enum class Rtc { ABSENT, UNBACKED, BACKED };

// A soft reset keeps the RTC chip's registers; a power-on reloads them.
void boot(Rtc rtc, esp_reset_reason_t reason, bool powerOn) {
  hostTimeResetReason() = reason;
  M5.board = rtc == Rtc::BACKED ? m5::board_t::board_M5StickC : m5::board_t::board_M5StickS3;
  M5.Rtc.enabled = rtc != Rtc::ABSENT;
  M5.Rtc.voltageLow = false;
  if (powerOn) {
    M5.Rtc.value.date = {2024, 1, 1};
    M5.Rtc.value.time = {12, 0, 0};
  }
  Furble::TimeKeeper::getInstance().resetForTest();
  Furble::TimeKeeper::init();
}

bool clockSurvives(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_DEEPSLEEP:
    case ESP_RST_USB:
      return true;
    default:
      return false;
  }
}

}  // namespace

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

  // Soft reset matrix: every reset reason, a retained record that validates
  // or a system clock lost behind it, and no RTC chip, an unbacked one or a
  // backed one. GPS set the clock two hours before the reset.
  constexpr uint64_t twoHoursMs = 2ULL * 60ULL * 60ULL * 1000ULL;
  constexpr uint32_t nvsUncertainty = 1000 + Furble::TimeKeeperPolicy::NVS_RESTORE_UNCERTAINTY_MS;
  constexpr uint32_t keptUncertainty =
      1000 + 720 + Furble::TimeKeeperPolicy::SOFT_RESET_UNCERTAINTY_MS;
  for (const Rtc rtc : {Rtc::ABSENT, Rtc::UNBACKED, Rtc::BACKED}) {
    for (const bool recordValid : {true, false}) {
      for (int raw = ESP_RST_UNKNOWN; raw <= ESP_RST_CPU_LOCKUP; raw++) {
        const auto reason = static_cast<esp_reset_reason_t>(raw);
        nvs_test_reset();
        powerOnClock();
        boot(rtc, ESP_RST_POWERON, true);
        CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
        CHECK(nvs_test_commit_count() == 1);
        setTimeMs(twoHoursMs);
        if (recordValid) {
          softResetClock();
        } else {
          powerOnClock();
        }
        boot(rtc, reason, false);
        const auto status = keeper.status();
        CHECK(status.valid);
        CHECK(nvs_test_commit_count() == 1);
        if (rtc == Rtc::ABSENT && recordValid && clockSurvives(reason)) {
          // The kept clock is demoted to RTC authority.
          CHECK(status.source == Furble::TimeSource::RTC);
          CHECK(status.epoch_us == epoch + twoHoursMs * 1000ULL);
          CHECK(status.uncertainty_ms == keptUncertainty);
        } else if (rtc != Rtc::ABSENT) {
          // M5.begin() already loaded the chip into the system clock. The
          // RTC path is unchanged.
          CHECK(status.source == Furble::TimeSource::RTC);
          CHECK(status.epoch_us == epoch);
          CHECK(status.uncertainty_ms == (rtc == Rtc::BACKED ? 2000U : 10U * 60U * 1000U));
        } else {
          // Power-on, brownout and the other unproven resets keep the
          // checkpoint and its one-hour penalty.
          CHECK(status.source == Furble::TimeSource::NVS);
          CHECK(status.epoch_us == epoch);
          CHECK(status.uncertainty_ms == nvsUncertainty);
        }
      }
    }
  }

  // An NVS-seeded clock is kept too. The old path rewound it to the stale
  // checkpoint and added another hour on every soft reset. Resets chain.
  nvs_test_reset();
  powerOnClock();
  boot(Rtc::ABSENT, ESP_RST_POWERON, true);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  powerOnClock();
  boot(Rtc::ABSENT, ESP_RST_POWERON, true);
  CHECK(keeper.status().source == Furble::TimeSource::NVS);
  setTimeMs(twoHoursMs);
  softResetClock();
  boot(Rtc::ABSENT, ESP_RST_SW, false);
  CHECK(keeper.status().source == Furble::TimeSource::NVS);
  CHECK(keeper.status().epoch_us == epoch + twoHoursMs * 1000ULL);
  CHECK(keeper.status().uncertainty_ms
        == nvsUncertainty + 720 + Furble::TimeKeeperPolicy::SOFT_RESET_UNCERTAINTY_MS);
  setTimeMs(twoHoursMs / 2);
  softResetClock();
  boot(Rtc::ABSENT, ESP_RST_PANIC, false);
  CHECK(keeper.status().epoch_us == epoch + 3ULL * twoHoursMs * 500ULL);
  CHECK(keeper.status().uncertainty_ms
        == nvsUncertainty + 720 + 360 + 2U * Furble::TimeKeeperPolicy::SOFT_RESET_UNCERTAINTY_MS);
  CHECK(nvs_test_commit_count() == 1);

  // Lockout regression: the RC-carried clock comes back 6 s fast. The next
  // GPS fix must still correct it, backwards, and the correction is retained.
  nvs_test_reset();
  powerOnClock();
  boot(Rtc::ABSENT, ESP_RST_POWERON, true);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch, 1000));
  setTimeMs(twoHoursMs);
  softResetClock();
  system_base_us += 6000000ULL;
  boot(Rtc::ABSENT, ESP_RST_PANIC, false);
  CHECK(keeper.status().source == Furble::TimeSource::RTC);
  CHECK(keeper.status().epoch_us == epoch + twoHoursMs * 1000ULL + 6000000ULL);
  CHECK(keeper.update(Furble::TimeSource::GPS, epoch + twoHoursMs * 1000ULL, 1000));
  CHECK(keeper.status().source == Furble::TimeSource::GPS);
  CHECK(keeper.status().epoch_us == epoch + twoHoursMs * 1000ULL);
  CHECK(furble_sim_system_time_us() == epoch + twoHoursMs * 1000ULL);

  // A boot without time clears the record. A clock set behind TimeKeeper's
  // back is not trusted after the next soft reset.
  nvs_test_reset();
  powerOnClock();
  boot(Rtc::ABSENT, ESP_RST_POWERON, true);
  CHECK(!keeper.status().valid);
  furble_sim_set_system_time_us(epoch);
  setTimeMs(1000);
  softResetClock();
  boot(Rtc::ABSENT, ESP_RST_SW, false);
  CHECK(!keeper.status().valid);
  hostTimeResetReason() = ESP_RST_POWERON;
  return 0;
}
