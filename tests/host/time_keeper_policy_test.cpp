#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "FurbleTimeKeeper.h"

using Furble::TimeSample;
using Furble::TimeSource;
using namespace Furble::TimeKeeperPolicy;

#define CHECK(condition)                                                                   \
  do {                                                                                     \
    if (!(condition)) {                                                                    \
      std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      return 1;                                                                            \
    }                                                                                      \
  } while (false)

int main() {
  constexpr uint64_t noon = 1704110400000000ULL;  // 2024-01-01 12:00:00 UTC
  TimeSample nvs {noon, 60U * 60U * 1000U, TimeSource::NVS, 1000};
  TimeSample rtc {noon + 2000000ULL, 2000, TimeSource::RTC, 2000};
  TimeSample gps {noon + 4000000ULL, 1000, TimeSource::GPS, 3000};
  TimeSample ntp {noon + 5000000ULL, 500, TimeSource::NTP, 4000};

  CHECK(valid(nvs));
  CHECK(shouldAccept(nullptr, nvs, 1000));
  CHECK(shouldAccept(&nvs, rtc, 2000));
  CHECK(shouldAccept(&rtc, gps, 3000));
  CHECK(shouldAccept(&gps, ntp, 4000));
  CHECK(sourcePriority(TimeSource::GPS) == sourcePriority(TimeSource::NTP));

  // A fallback cannot displace an established authoritative clock.
  TimeSample staleFallback {noon + 120000000ULL, 3600000, TimeSource::NVS, 5000};
  CHECK(!shouldAccept(&ntp, staleFallback, 5000));

  // A lower/equal authority source cannot move time backwards by minutes.
  TimeSample backwards {noon - 600000000ULL, 1000, TimeSource::GPS, 6000};
  CHECK(!shouldAccept(&ntp, backwards, 6000));

  // Monotonic drift is projected without writing flash.
  CHECK(predictedEpochUs(ntp, 6400) == ntp.epoch_us + 2400000ULL);
  CHECK(predictedUncertaintyMs(ntp, 24004000) == ntp.uncertainty_ms + 2400U);
  CHECK(!shouldPersist(&ntp, {ntp.epoch_us + 1000000ULL, 500, TimeSource::NTP, 5000}, 5000));
  CHECK(shouldPersist(&ntp, {ntp.epoch_us + 60000000ULL * 1000ULL, 500, TimeSource::NTP, 5000},
                      5000));
  CHECK(shouldPersist(&nvs, gps, 3000));
  const uint64_t expiry_ms =
      (static_cast<uint64_t>(MAX_UNCERTAINTY_MS) * 1000000ULL) / UNCERTAINTY_GROWTH_PPM;
  CHECK(!validAt(ntp, ntp.monotonic_ms + expiry_ms));
  CHECK(predictedUncertaintyMs(ntp, std::numeric_limits<uint64_t>::max()) == MAX_UNCERTAINTY_MS);
  CHECK(predictedEpochUs(ntp, std::numeric_limits<uint64_t>::max())
        == std::numeric_limits<uint64_t>::max());
  CHECK(predictedEpochUs(ntp, ntp.monotonic_ms - 1) == ntp.epoch_us);

  std::array<uint8_t, 64> wire = {};
  const size_t size = encode(gps, wire.data(), wire.size());
  CHECK(size > 0);
  TimeSample decoded = {};
  CHECK(decode(wire.data(), size, decoded));
  CHECK(decoded.epoch_us == gps.epoch_us);
  wire[7] ^= 0x01;
  CHECK(!decode(wire.data(), size, decoded));

  uint64_t epoch = 0;
  CHECK(utcToEpochUs(2024, 2, 29, 23, 59, 58, 12, epoch));
  CHECK(epoch == 1709251198120000ULL);
  CHECK(!utcToEpochUs(2023, 2, 29, 0, 0, 0, 0, epoch));
  CHECK(!utcToEpochUs(2024, 1, 1, 24, 0, 0, 0, epoch));
  CHECK(!utcToEpochUs(2100, 1, 1, 0, 0, 0, 0, epoch));
  CHECK(utcToEpochUs(1970, 1, 1, 0, 0, 0, 0, epoch) && epoch == 0);

  // A reboot has a new monotonic origin. The persisted epoch remains usable,
  // but the explicit restore uncertainty makes the loss of elapsed time clear.
  TimeSample rebooted = decoded;
  rebooted.monotonic_ms = 0;
  rebooted.uncertainty_ms += NVS_RESTORE_UNCERTAINTY_MS;
  CHECK(valid(rebooted));
  CHECK(predictedEpochUs(rebooted, 0) == rebooted.epoch_us);
  CHECK(predictedEpochUs(rebooted, 5000) == rebooted.epoch_us + 5000000ULL);
  TimeSample unknown = nvs;
  unknown.source = static_cast<TimeSource>(255);
  CHECK(!valid(unknown));

  // A soft reset trusts the running system clock. Two hours at 100 ppm add
  // 720 ms, plus the fixed one-second hand-off estimate. GPS is demoted to RTC.
  std::array<uint8_t, RECORD_BYTES> retained = {};
  CHECK(encode(gps, retained.data(), retained.size()) == RECORD_BYTES);
  constexpr uint64_t twoHoursUs = 2ULL * 60ULL * 60ULL * 1000000ULL;
  TimeSample kept = {};
  CHECK(restoreRetained(retained.data(), retained.size(), gps.epoch_us + twoHoursUs, kept));
  CHECK(kept.epoch_us == gps.epoch_us + twoHoursUs);
  CHECK(kept.uncertainty_ms == gps.uncertainty_ms + 720 + SOFT_RESET_UNCERTAINTY_MS);
  CHECK(kept.source == TimeSource::RTC);
  CHECK(kept.monotonic_ms == 0);

  // Regression for the kept-clock lockout: a kept clock 6 s fast must still
  // accept the next GPS fix. With the GPS source kept, the 5 s backward
  // tolerance rejected every equal-priority fix until the seven-day ceiling.
  TimeSample fast = {};
  CHECK(restoreRetained(retained.data(), retained.size(), gps.epoch_us + 6000000ULL, fast));
  const TimeSample fix = {gps.epoch_us, 1000, TimeSource::GPS, 0};
  CHECK(shouldAccept(&fast, fix, 0));
  const TimeSample companion = {gps.epoch_us, 5000, TimeSource::COMPANION, 0};
  CHECK(shouldAccept(&fast, companion, 0));

  // An NVS-seeded record is never promoted by a soft reset.
  std::array<uint8_t, RECORD_BYTES> fromNvs = {};
  CHECK(encode(nvs, fromNvs.data(), fromNvs.size()) == RECORD_BYTES);
  TimeSample keptNvs = {};
  CHECK(restoreRetained(fromNvs.data(), fromNvs.size(), nvs.epoch_us, keptNvs));
  CHECK(keptNvs.source == TimeSource::NVS);
  // No elapsed time still pays the hand-off estimate.
  CHECK(restoreRetained(retained.data(), retained.size(), gps.epoch_us, kept));
  CHECK(kept.uncertainty_ms == gps.uncertainty_ms + SOFT_RESET_UNCERTAINTY_MS);

  // Fail closed: a clock behind the record, a wrong length, a corrupted
  // record and an elapsed time past the uncertainty ceiling.
  const TimeSample untouched = kept;
  CHECK(!restoreRetained(retained.data(), retained.size(), gps.epoch_us - 1, kept));
  CHECK(!restoreRetained(retained.data(), retained.size(), 0, kept));
  CHECK(!restoreRetained(retained.data(), retained.size() - 1, gps.epoch_us, kept));
  CHECK(!restoreRetained(nullptr, retained.size(), gps.epoch_us, kept));
  for (size_t byte = 0; byte < retained.size(); byte++) {
    std::array<uint8_t, RECORD_BYTES> corrupted = retained;
    corrupted[byte] ^= 0x10;
    CHECK(!restoreRetained(corrupted.data(), corrupted.size(), gps.epoch_us, kept));
  }
  std::array<uint8_t, RECORD_BYTES> erased = {};
  CHECK(!restoreRetained(erased.data(), erased.size(), gps.epoch_us, kept));
  // 7 days at 100 ppm needs about 192 years of elapsed time. A clock that far
  // ahead is past MAX_EPOCH_SECONDS, so the restored sample is invalid too.
  CHECK(!restoreRetained(retained.data(), retained.size(), MAX_EPOCH_SECONDS * 1000000ULL, kept));
  TimeSample loose = gps;
  loose.uncertainty_ms = MAX_UNCERTAINTY_MS - 500;
  CHECK(encode(loose, retained.data(), retained.size()) == RECORD_BYTES);
  CHECK(!restoreRetained(retained.data(), retained.size(), gps.epoch_us, kept));
  CHECK(kept.epoch_us == untouched.epoch_us && kept.uncertainty_ms == untouched.uncertainty_ms);

  // RTC retention (plan 350). A battery-backed calendar is trusted to have
  // kept time across a power-off, so a clear voltage-low flag lets it seed the
  // clock with a two-second window. On an unbacked RTC the flag says nothing
  // the checkpoint policy does not already cover, so that value keeps the
  // ten-minute window and the flag is ignored.
  CHECK(RTC_BACKED_UNCERTAINTY_MS == 2000);
  CHECK(RTC_UNBACKED_UNCERTAINTY_MS == 10U * 60U * 1000U);
  CHECK(rtcUncertaintyMs(true) == RTC_BACKED_UNCERTAINTY_MS);
  CHECK(rtcUncertaintyMs(false) == RTC_UNBACKED_UNCERTAINTY_MS);
  CHECK(rtcValueTrusted(true, false));
  CHECK(!rtcValueTrusted(true, true));
  CHECK(rtcValueTrusted(false, false));
  CHECK(rtcValueTrusted(false, true));

  // Timed wake prediction (plan 320 C2). A timed power-off knows the interval
  // it armed, so the clock at the wake instant is predicted rather than
  // restored. The bound is 2 % of the interval plus a two second base, on top
  // of the uncertainty the clock already carried.
  CHECK(sourcePriority(TimeSource::NVS) < sourcePriority(TimeSource::TIMED_WAKE));
  CHECK(sourcePriority(TimeSource::TIMED_WAKE) < sourcePriority(TimeSource::RTC));
  CHECK(sourcePriority(TimeSource::TIMED_WAKE) < sourcePriority(TimeSource::GPS));
  CHECK(valid(TimeSample {noon, 1000, TimeSource::TIMED_WAKE, 0}));

  // A two hour interval: 2 % is 144 s, plus the 2 s base.
  CHECK(timedWakeUncertaintyMs(0, 7200) == 144000U + TIMED_WAKE_BASE_MS);
  // The interval is bounded and a zero interval means there was no timer.
  CHECK(timedWakeUncertaintyMs(1000, 0) == 1000U + TIMED_WAKE_BASE_MS);
  CHECK(timedWakeUncertaintyMs(MAX_UNCERTAINTY_MS, 3600) == MAX_UNCERTAINTY_MS);
  CHECK(TIMED_WAKE_MAX_SLEEP_S == 24U * 60U * 60U);

  TimeSample prediction = {};
  CHECK(predictTimedWake(gps, 3000, 7200, prediction));
  CHECK(prediction.source == TimeSource::TIMED_WAKE);
  CHECK(prediction.epoch_us == gps.epoch_us + 7200ULL * 1000000ULL);
  CHECK(prediction.uncertainty_ms == 1000U + 144000U + TIMED_WAKE_BASE_MS);
  CHECK(prediction.monotonic_ms == 3000);
  // The prediction carries the source that produced it through the same
  // versioned record as every other sample, so the boot path needs no second
  // decoder.
  std::array<uint8_t, RECORD_BYTES> wake = {};
  CHECK(encode(prediction, wake.data(), wake.size()) == RECORD_BYTES);

  // Fail closed: a stopped clock, a zero interval, an interval past the day,
  // and an uncertainty that the addition would push over the ceiling.
  CHECK(!predictTimedWake(nvs, 0, 0, prediction));
  CHECK(!predictTimedWake(nvs, 0, TIMED_WAKE_MAX_SLEEP_S + 1, prediction));
  TimeSample expired = gps;
  expired.uncertainty_ms = MAX_UNCERTAINTY_MS - 100;
  CHECK(!predictTimedWake(expired, 3000, 7200, prediction));
  TimeSample invalid = gps;
  invalid.source = TimeSource::NONE;
  CHECK(!predictTimedWake(invalid, 3000, 3600, prediction));
  CHECK(prediction.source == TimeSource::TIMED_WAKE);

  // Adoption adds the boot time that the timer measured, and nothing else: a
  // confirmed wake already bounds the interval.
  TimeSample adopted = {};
  CHECK(adoptTimedWake(wake.data(), wake.size(), 4000, &nvs, adopted));
  CHECK(adopted.source == TimeSource::TIMED_WAKE);
  CHECK(adopted.epoch_us == prediction.epoch_us + 4000000ULL);
  CHECK(adopted.uncertainty_ms == prediction.uncertainty_ms);
  CHECK(adopted.monotonic_ms == 4000);
  CHECK(validAt(adopted, 4000));
  // No checkpoint at boot is allowed: a first power-on has nothing stored.
  CHECK(adoptTimedWake(wake.data(), wake.size(), 4000, nullptr, adopted));
  // A checkpoint that reaches past the prediction cannot have been written
  // before the shutdown the record describes, so the record is refused.
  TimeSample ahead = nvs;
  ahead.epoch_us = prediction.epoch_us + 1000000ULL;
  CHECK(!adoptTimedWake(wake.data(), wake.size(), 4000, &ahead, adopted));

  // Fail closed: another source's record, a truncated or corrupted record, an
  // erased one, and a boot time that would leave the accepted epoch range.
  std::array<uint8_t, RECORD_BYTES> notWake = {};
  CHECK(encode(nvs, notWake.data(), notWake.size()) == RECORD_BYTES);
  CHECK(!adoptTimedWake(notWake.data(), notWake.size(), 0, nullptr, adopted));
  CHECK(!adoptTimedWake(wake.data(), wake.size() - 1, 0, nullptr, adopted));
  CHECK(!adoptTimedWake(nullptr, wake.size(), 0, nullptr, adopted));
  std::array<uint8_t, RECORD_BYTES> erasedWake = {};
  CHECK(!adoptTimedWake(erasedWake.data(), erasedWake.size(), 0, nullptr, adopted));
  for (size_t byte = 0; byte < wake.size(); byte++) {
    std::array<uint8_t, RECORD_BYTES> corrupted = wake;
    corrupted[byte] ^= 0x20;
    CHECK(!adoptTimedWake(corrupted.data(), corrupted.size(), 0, nullptr, adopted));
  }
  // A boot time so far ahead that the prediction would leave the accepted
  // epoch range is refused rather than wrapped.
  CHECK(!adoptTimedWake(wake.data(), wake.size(), std::numeric_limits<uint64_t>::max(), nullptr,
                        adopted));
  return 0;
}
