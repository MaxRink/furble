#include <array>
#include <cassert>
#include <cstdint>

#include "FurbleTimeKeeper.h"

using Furble::TimeSample;
using Furble::TimeSource;
using namespace Furble::TimeKeeperPolicy;

int main() {
  constexpr uint64_t noon = 1704110400000000ULL;  // 2024-01-01 12:00:00 UTC
  TimeSample nvs {noon, 60U * 60U * 1000U, TimeSource::NVS, 1000};
  TimeSample rtc {noon + 2000000ULL, 2000, TimeSource::RTC, 2000};
  TimeSample gps {noon + 4000000ULL, 1000, TimeSource::GPS, 3000};
  TimeSample ntp {noon + 5000000ULL, 500, TimeSource::NTP, 4000};

  assert(valid(nvs));
  assert(shouldAccept(nullptr, nvs, 1000));
  assert(shouldAccept(&nvs, rtc, 2000));
  assert(shouldAccept(&rtc, gps, 3000));
  assert(shouldAccept(&gps, ntp, 4000));

  // A fallback cannot displace an established authoritative clock.
  TimeSample staleFallback {noon + 120000000ULL, 3600000, TimeSource::NVS, 5000};
  assert(!shouldAccept(&ntp, staleFallback, 5000));

  // A lower/equal authority source cannot move time backwards by minutes.
  TimeSample backwards {noon - 600000000ULL, 1000, TimeSource::GPS, 6000};
  assert(!shouldAccept(&ntp, backwards, 6000));

  // Monotonic drift is projected without writing flash.
  assert(predictedEpochUs(ntp, 6400) == ntp.epoch_us + 2400000ULL);
  assert(!shouldPersist(&ntp, {ntp.epoch_us + 1000000ULL, 500, TimeSource::NTP, 5000}, 5000));
  assert(shouldPersist(&ntp, {ntp.epoch_us + 60000000ULL * 1000ULL, 500, TimeSource::NTP, 5000},
                       5000));
  assert(shouldPersist(&nvs, gps, 3000));

  std::array<uint8_t, 64> wire = {};
  const size_t size = encode(gps, wire.data(), wire.size());
  assert(size > 0);
  TimeSample decoded = {};
  assert(decode(wire.data(), size, decoded));
  assert(decoded.epoch_us == gps.epoch_us);
  wire[7] ^= 0x01;
  assert(!decode(wire.data(), size, decoded));

  uint64_t epoch = 0;
  assert(utcToEpochUs(2024, 2, 29, 23, 59, 58, 12, epoch));
  assert(epoch == 1709251198120000ULL);
  assert(!utcToEpochUs(2023, 2, 29, 0, 0, 0, 0, epoch));
  assert(!utcToEpochUs(2024, 1, 1, 24, 0, 0, 0, epoch));

  // A reboot has a new monotonic origin. The persisted epoch remains usable,
  // but the explicit restore uncertainty makes the loss of elapsed time clear.
  TimeSample rebooted = decoded;
  rebooted.monotonic_ms = 0;
  rebooted.uncertainty_ms += NVS_RESTORE_UNCERTAINTY_MS;
  assert(valid(rebooted));
  assert(predictedEpochUs(rebooted, 5000) == rebooted.epoch_us + 5000000ULL);
  return 0;
}
