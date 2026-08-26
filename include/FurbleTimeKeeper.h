#ifndef FURBLE_TIME_KEEPER_H
#define FURBLE_TIME_KEEPER_H

#include <cstddef>
#include <cstdint>

namespace Furble {

/** Sources ordered from least to most authoritative. */
enum class TimeSource : uint8_t {
  NONE = 0,
  NVS = 1,
  RTC = 2,
  COMPANION = 3,
  GPS = 4,
  NTP = 5,
};

struct TimeSample {
  uint64_t epoch_us;
  uint32_t uncertainty_ms;
  TimeSource source;
  uint64_t monotonic_ms;
};

/** Pure validation, arbitration, and persistence codec used by firmware and sim. */
namespace TimeKeeperPolicy {

constexpr uint64_t MIN_EPOCH_SECONDS = 1577836800ULL;  // 2020-01-01
constexpr uint64_t MAX_EPOCH_SECONDS = 4102444800ULL;  // 2100-01-01
constexpr uint32_t MAX_UNCERTAINTY_MS = 7U * 24U * 60U * 60U * 1000U;
constexpr uint32_t NVS_RESTORE_UNCERTAINTY_MS = 60U * 60U * 1000U;
// Bound the free-running ESP timer with a conservative 100 ppm oscillator
// error.  The elapsed monotonic interval is known; it is not a one-for-one
// wall-clock uncertainty increase.
constexpr uint32_t UNCERTAINTY_GROWTH_PPM = 100;
constexpr uint32_t BACKWARD_TOLERANCE_MS = 5000;
constexpr uint32_t MEANINGFUL_CORRECTION_MS = 60U * 1000U;

uint8_t sourcePriority(TimeSource source);
bool valid(const TimeSample &sample);
bool validAt(const TimeSample &sample, uint64_t monotonic_ms);
uint64_t predictedEpochUs(const TimeSample &sample, uint64_t monotonic_ms);
uint32_t predictedUncertaintyMs(const TimeSample &sample, uint64_t monotonic_ms);
bool shouldAccept(const TimeSample *current, const TimeSample &candidate, uint64_t monotonic_ms);
bool shouldPersist(const TimeSample *persisted, const TimeSample &candidate, uint64_t monotonic_ms);

/** Encode/decode a versioned CRC-protected NVS record. */
size_t encode(const TimeSample &sample, uint8_t *buffer, size_t capacity);
bool decode(const uint8_t *buffer, size_t length, TimeSample &sample);

/** Convert a UTC calendar tuple to Unix microseconds. */
bool utcToEpochUs(uint16_t year,
                  uint8_t month,
                  uint8_t day,
                  uint8_t hour,
                  uint8_t minute,
                  uint8_t second,
                  uint8_t centisecond,
                  uint64_t &epoch_us);

}  // namespace TimeKeeperPolicy

/**
 * Wall-clock service. The monotonic timer remains the source for durations.
 * Wall time is seeded from an external RTC or the NVS record and corrected by
 * GPS, NTP, or a companion time sample.
 */
class TimeKeeper {
 public:
  struct status_t {
    bool valid;
    bool rtc_available;
    bool rtc_battery_backed;
    uint64_t epoch_us;
    uint32_t uncertainty_ms;
    TimeSource source;
    uint32_t nvs_write_count;
  };

  static TimeKeeper &getInstance();
  static void init(void);

  bool update(TimeSource source, uint64_t epoch_us, uint32_t uncertainty_ms);
  bool updateUtc(TimeSource source,
                 uint16_t year,
                 uint8_t month,
                 uint8_t day,
                 uint8_t hour,
                 uint8_t minute,
                 uint8_t second,
                 uint8_t centisecond,
                 uint32_t uncertainty_ms);

  status_t status(void) const;
  bool isValid(void) const;
  uint64_t nowEpochUs(void) const;

  /** Persist a final coherent value before reset or power-off. */
  void flush(void);

 private:
  TimeKeeper() = default;
  TimeKeeper(const TimeKeeper &) = delete;
  TimeKeeper &operator=(const TimeKeeper &) = delete;
};

}  // namespace Furble

#endif
