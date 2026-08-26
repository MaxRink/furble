#include "FurbleTimeKeeper.h"

#include <cstring>
#include <limits>

namespace Furble::TimeKeeperPolicy {
namespace {

constexpr uint32_t RECORD_MAGIC = 0x4654494d;  // "FTIM"
constexpr uint16_t RECORD_VERSION = 1;

struct __attribute__((packed)) WireRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint64_t epoch_us;
  uint32_t uncertainty_ms;
  uint8_t source;
  uint8_t reserved[3];
  uint32_t crc;
};

static_assert(sizeof(WireRecord) == 28, "time record layout changed");

uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return ~crc;
}

bool leap(uint16_t year) {
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (month == 2 && leap(year)) ? 29 : days[month - 1];
}

uint64_t daysFromCivil(uint16_t year, uint8_t month, uint8_t day) {
  int64_t y = year - (month <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t mp = month + (month > 2 ? -3 : 9);
  const uint32_t doy = (153 * mp + 2) / 5 + day - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<uint64_t>(era * 146097 + static_cast<int64_t>(doe) - 719468);
}

}  // namespace

uint8_t sourcePriority(TimeSource source) {
  // GPS and NTP are both authoritative UTC sources. Keep them at the same
  // level so whichever one arrives first cannot be displaced by a less
  // reliable fallback, while either can correct RTC/NVS state.
  switch (source) {
    case TimeSource::GPS:
    case TimeSource::NTP:
      return 5;
    default:
      return static_cast<uint8_t>(source);
  }
}

bool valid(const TimeSample &sample) {
  return sample.source != TimeSource::NONE && sample.source <= TimeSource::NTP
         && sample.epoch_us >= MIN_EPOCH_SECONDS * 1000000ULL
         && sample.epoch_us < MAX_EPOCH_SECONDS * 1000000ULL
         && sample.uncertainty_ms <= MAX_UNCERTAINTY_MS;
}

uint32_t predictedUncertaintyMs(const TimeSample &sample, uint64_t monotonic_ms) {
  if (monotonic_ms <= sample.monotonic_ms) {
    return sample.uncertainty_ms;
  }
  const uint64_t elapsed = monotonic_ms - sample.monotonic_ms;
  const uint64_t growth =
      elapsed > (std::numeric_limits<uint64_t>::max() - 999999ULL) / UNCERTAINTY_GROWTH_PPM
          ? std::numeric_limits<uint64_t>::max()
          : (elapsed * UNCERTAINTY_GROWTH_PPM + 999999ULL) / 1000000ULL;
  if (growth >= MAX_UNCERTAINTY_MS
      || sample.uncertainty_ms > MAX_UNCERTAINTY_MS - growth) {
    return MAX_UNCERTAINTY_MS;
  }
  return sample.uncertainty_ms + static_cast<uint32_t>(growth);
}

bool validAt(const TimeSample &sample, uint64_t monotonic_ms) {
  return valid(sample) && predictedUncertaintyMs(sample, monotonic_ms) < MAX_UNCERTAINTY_MS;
}

uint64_t predictedEpochUs(const TimeSample &sample, uint64_t monotonic_ms) {
  if (monotonic_ms <= sample.monotonic_ms) {
    return sample.epoch_us;
  }
  const uint64_t elapsed = monotonic_ms - sample.monotonic_ms;
  const uint64_t maxDelta = std::numeric_limits<uint64_t>::max() - sample.epoch_us;
  if (elapsed > maxDelta / 1000ULL) {
    return std::numeric_limits<uint64_t>::max();
  }
  return sample.epoch_us + elapsed * 1000ULL;
}

bool shouldAccept(const TimeSample *current, const TimeSample &candidate, uint64_t monotonic_ms) {
  if (!valid(candidate)) {
    return false;
  }
  if (current == nullptr || !validAt(*current, monotonic_ms)) {
    return true;
  }

  const uint64_t expected = predictedEpochUs(*current, monotonic_ms);
  const uint64_t expected_ms_unsigned = expected / 1000ULL;
  const int64_t expected_ms =
      expected_ms_unsigned > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
          ? std::numeric_limits<int64_t>::max()
          : static_cast<int64_t>(expected_ms_unsigned);
  const int64_t correction_ms = static_cast<int64_t>(candidate.epoch_us / 1000ULL) - expected_ms;
  // A lower-priority source cannot move an established wall clock backwards.
  // Higher-priority sources may correct a stale fallback, but never accept a
  // large reverse jump from a source of equal or lower authority.
  if ((correction_ms < -static_cast<int64_t>(BACKWARD_TOLERANCE_MS))
      && sourcePriority(candidate.source) <= sourcePriority(current->source)) {
    return false;
  }
  if (sourcePriority(candidate.source) < sourcePriority(current->source)) {
    return false;
  }
  return true;
}

bool shouldPersist(const TimeSample *persisted,
                   const TimeSample &candidate,
                   uint64_t monotonic_ms) {
  if (!valid(candidate)) {
    return false;
  }
  if (persisted == nullptr || !validAt(*persisted, monotonic_ms)) {
    return true;
  }
  if (sourcePriority(candidate.source) > sourcePriority(persisted->source)) {
    return true;
  }
  const uint64_t expected = predictedEpochUs(*persisted, monotonic_ms);
  const uint64_t delta =
      candidate.epoch_us > expected ? candidate.epoch_us - expected : expected - candidate.epoch_us;
  return delta >= static_cast<uint64_t>(MEANINGFUL_CORRECTION_MS) * 1000ULL;
}

size_t encode(const TimeSample &sample, uint8_t *buffer, size_t capacity) {
  if (buffer == nullptr || capacity < sizeof(WireRecord) || !valid(sample)) {
    return 0;
  }
  WireRecord record = {};
  record.magic = RECORD_MAGIC;
  record.version = RECORD_VERSION;
  record.size = sizeof(record);
  record.epoch_us = sample.epoch_us;
  record.uncertainty_ms = sample.uncertainty_ms;
  record.source = static_cast<uint8_t>(sample.source);
  record.crc =
      crc32(reinterpret_cast<const uint8_t *>(&record), sizeof(record) - sizeof(record.crc));
  std::memcpy(buffer, &record, sizeof(record));
  return sizeof(record);
}

bool decode(const uint8_t *buffer, size_t length, TimeSample &sample) {
  if (buffer == nullptr || length != sizeof(WireRecord)) {
    return false;
  }
  WireRecord record = {};
  std::memcpy(&record, buffer, sizeof(record));
  if (record.magic != RECORD_MAGIC || record.version != RECORD_VERSION
      || record.size != sizeof(record)
      || record.reserved[0] != 0 || record.reserved[1] != 0 || record.reserved[2] != 0
      || crc32(reinterpret_cast<const uint8_t *>(&record), sizeof(record) - sizeof(record.crc))
             != record.crc) {
    return false;
  }
  const TimeSource source = static_cast<TimeSource>(record.source);
  if (source == TimeSource::NONE || source > TimeSource::NTP) {
    return false;
  }
  sample = {record.epoch_us, record.uncertainty_ms, source, 0};
  return valid(sample);
}

bool utcToEpochUs(uint16_t year,
                  uint8_t month,
                  uint8_t day,
                  uint8_t hour,
                  uint8_t minute,
                  uint8_t second,
                  uint8_t centisecond,
                  uint64_t &epoch_us) {
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)
      || hour > 23 || minute > 59 || second > 59 || centisecond > 99) {
    return false;
  }
  const uint64_t days = daysFromCivil(year, month, day);
  const uint64_t seconds = days * 86400ULL + hour * 3600ULL + minute * 60ULL + second;
  if (seconds >= MAX_EPOCH_SECONDS) {
    return false;
  }
  epoch_us = seconds * 1000000ULL + centisecond * 10000ULL;
  return true;
}

}  // namespace Furble::TimeKeeperPolicy
