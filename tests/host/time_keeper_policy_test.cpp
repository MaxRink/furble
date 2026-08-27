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
  return 0;
}
