#ifndef FURBLE_TIME_KEEPER_H
#define FURBLE_TIME_KEEPER_H

#include <cstddef>
#include <cstdint>

namespace Furble {

/**
 * Wall-clock sources.
 *
 * The wire record stores the value, so a new source appends here and takes its
 * authority from sourcePriority() rather than from its position.
 */
enum class TimeSource : uint8_t {
  NONE = 0,
  NVS = 1,
  RTC = 2,
  COMPANION = 3,
  GPS = 4,
  NTP = 5,
  // A clock predicted before a timed power-off and confirmed by the board's
  // wake marker. The interval comes from the programmed timer, not from a
  // measurement, so it ranks above a stored checkpoint and below a calendar
  // chip.
  TIMED_WAKE = 6,
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
// A soft reset hands the running clock across the boot. The RTC timer carries
// it on the RC slow clock, converted with the last calibration from boot or
// light sleep entry. The error since that calibration is unmeasured, so this
// one second and the 100 ppm growth below are an estimate, not a bound. A
// separate growth constant waits for the StickS3 measurement in plan 139.
constexpr uint32_t SOFT_RESET_UNCERTAINTY_MS = 1000;
// Bound the free-running ESP timer with a conservative 100 ppm oscillator
// error.  The elapsed monotonic interval is known; it is not a one-for-one
// wall-clock uncertainty increase.
constexpr uint32_t UNCERTAINTY_GROWTH_PPM = 100;
constexpr uint32_t BACKWARD_TOLERANCE_MS = 5000;
constexpr uint32_t MEANINGFUL_CORRECTION_MS = 60U * 1000U;
// Normal synchronization writes are limited to four per 24 hours. A graceful
// shutdown may add a checkpoint after three and a half hours. The fractional
// margin keeps the hard bound strict for inclusive rolling 24-hour windows.
constexpr uint64_t NVS_NORMAL_MIN_WRITE_INTERVAL_MS = 6ULL * 60ULL * 60ULL * 1000ULL;
constexpr uint64_t NVS_CHECKPOINT_MIN_WRITE_INTERVAL_MS =
    3ULL * 60ULL * 60ULL * 1000ULL + 30ULL * 60ULL * 1000ULL;

// An RTC calendar that survives a power-off still pays the read and its
// one-second register resolution, so a backed read carries a small window. An
// RTC whose rail is not retained can have stopped for an unknown time between
// checkpoints, so its value carries a ten-minute window instead.
constexpr uint32_t RTC_BACKED_UNCERTAINTY_MS = 2000;
constexpr uint32_t RTC_UNBACKED_UNCERTAINTY_MS = 10U * 60U * 1000U;

/** Uncertainty a value read from the RTC chip carries. */
constexpr uint32_t rtcUncertaintyMs(bool battery_backed) {
  return battery_backed ? RTC_BACKED_UNCERTAINTY_MS : RTC_UNBACKED_UNCERTAINTY_MS;
}

/**
 * Whether a read RTC value may seed the wall clock.
 *
 * The PCF8563 and BM8563 raise a voltage-low flag in register 0x02 bit 7 when
 * the backup supply fell below the retention threshold, so the calendar may
 * have stopped. Only a battery-backed RTC is trusted to have kept time across
 * that kind of outage, so only there does the flag reject the value; without
 * backing the flag says nothing extra and the unbacked uncertainty applies.
 */
constexpr bool rtcValueTrusted(bool battery_backed, bool voltage_low) {
  return !battery_backed || !voltage_low;
}

uint8_t sourcePriority(TimeSource source);
bool valid(const TimeSample &sample);
bool validAt(const TimeSample &sample, uint64_t monotonic_ms);
uint64_t predictedEpochUs(const TimeSample &sample, uint64_t monotonic_ms);
uint32_t predictedUncertaintyMs(const TimeSample &sample, uint64_t monotonic_ms);
bool shouldAccept(const TimeSample *current, const TimeSample &candidate, uint64_t monotonic_ms);
bool shouldPersist(const TimeSample *persisted, const TimeSample &candidate, uint64_t monotonic_ms);

/** Encode/decode a versioned CRC-protected NVS record. */
constexpr size_t RECORD_BYTES = 28;
size_t encode(const TimeSample &sample, uint8_t *buffer, size_t capacity);
bool decode(const uint8_t *buffer, size_t length, TimeSample &sample);

/**
 * Restore a record kept in RTC memory across a soft reset. The record holds
 * the last accepted sample, anchored where the system clock was set to it.
 * The running system clock is the new epoch. Its uncertainty grows by the
 * elapsed system time at UNCERTAINTY_GROWTH_PPM plus SOFT_RESET_UNCERTAINTY_MS,
 * an estimate rather than a bound. A clock that runs behind the record fails
 * closed. The source is demoted to at most RTC, so GPS, NTP and companion time
 * outrank the kept clock and correct it in either direction. Sets
 * monotonic_ms to zero.
 */
bool restoreRetained(const uint8_t *record,
                     size_t length,
                     uint64_t system_epoch_us,
                     TimeSample &sample);

/*
 * Timed wake prediction.
 *
 * A timed power-off knows the interval it armed, so the clock at the wake
 * instant is predicted instead of restored. The M5PM1 timer counts whole
 * seconds and neither its clock source nor its accuracy is published (M5PM1
 * manual V1.9, section 11), so the bound below is an estimate, not a measured
 * one: 2 % of the programmed interval plus a two second base for the
 * one-second timer resolution and the interval between the rail coming back
 * and the first read. Plan 320 records the bench run that would replace it.
 */
constexpr uint32_t TIMED_WAKE_PPM = 20000;
constexpr uint32_t TIMED_WAKE_BASE_MS = 2000;
// A day is longer than any resume window the intervalometer accepts, and the
// prediction is no better than a stored checkpoint past it, so no record is
// written for a longer interval.
constexpr uint32_t TIMED_WAKE_MAX_SLEEP_S = 24U * 60U * 60U;

/** Uncertainty a confirmed timed wake leaves, from its armed interval. */
uint32_t timedWakeUncertaintyMs(uint32_t uncertainty_ms, uint32_t programmed_seconds);

/**
 * Build the sample the wall clock should read when an armed timer fires.
 * The epoch is the current prediction advanced by the programmed interval and
 * the uncertainty is the current uncertainty grown by
 * timedWakeUncertaintyMs(). Fails closed, writing nothing, when the current
 * sample is unusable, the interval is zero or beyond TIMED_WAKE_MAX_SLEEP_S,
 * or the grown uncertainty reaches MAX_UNCERTAINTY_MS.
 */
bool predictTimedWake(const TimeSample &current,
                      uint64_t monotonic_ms,
                      uint32_t programmed_seconds,
                      TimeSample &prediction);

/**
 * Adopt a predicted wake sample after the board confirmed a timed wake.
 * boot_ms is the monotonic time at adoption, measured on the timer that runs
 * from boot, so only the prediction's own bound limits the result.
 *
 * Fails closed when the record does not decode, when it was not written by
 * predictTimedWake(), when it predates a stored checkpoint that a caller can
 * prove is newer (checkpoint may be null), or when the resulting sample is
 * outside the accepted epoch and uncertainty range. The caller must discard
 * the record afterwards so a second boot cannot reuse it.
 */
bool adoptTimedWake(const uint8_t *record,
                    size_t length,
                    uint64_t boot_ms,
                    const TimeSample *checkpoint,
                    TimeSample &sample);

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
 * GPS, NTP, or a companion time sample. On a board without an RTC chip, a soft
 * reset keeps the running system clock through a record in RTC memory.
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

  /**
   * Predict the wall clock at a timed power-on and write the record the board
   * keeps across it.
   *
   * Call immediately before arming a timed wake. The record is free to write:
   * it belongs in the board's power-off retained memory, never in NVS, so a
   * timelapse cycle cannot turn a timed wake into a flash write.
   *
   * @param programmed_seconds the interval the caller is about to arm
   * @param buffer receives up to TimeKeeperPolicy::RECORD_BYTES bytes
   * @param capacity size of buffer
   * @return bytes written, or 0 when no record was produced and the next boot
   *         keeps the stored checkpoint path.
   */
  size_t prepareTimedWake(uint32_t programmed_seconds, uint8_t *buffer, size_t capacity);

  /**
   * Adopt a prepared prediction after the board confirmed a timed wake.
   *
   * The caller must have verified the board's own wake marker, because that
   * marker is what makes the elapsed interval the one this record was built
   * from, and must discard the record afterwards.
   *
   * @return true when the wall clock was replaced by the prediction.
   */
  bool adoptTimedWake(const uint8_t *buffer, size_t length);

  /** Persist a final coherent value before reset or power-off. */
  void flush(void);

#if defined(FURBLE_SIM)
  /** Reset process state while retaining the simulated NVS store. */
  void resetForTest(void);
#endif

 private:
  TimeKeeper() = default;
  TimeKeeper(const TimeKeeper &) = delete;
  TimeKeeper &operator=(const TimeKeeper &) = delete;
};

}  // namespace Furble

#endif
