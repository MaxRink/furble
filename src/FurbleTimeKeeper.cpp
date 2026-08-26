#include "FurbleTimeKeeper.h"

#include <array>
#include <ctime>
#include <mutex>

#include <M5Unified.h>
#include <esp_timer.h>
#include <sys/time.h>

#include "FurbleTypes.h"
#include "Preferences.h"

namespace Furble {
namespace {

constexpr const char *LOG_TAG = "time";
constexpr const char *NVS_KEY = "time_state";
constexpr const char *NVS_NAMESPACE = FURBLE_STR;
constexpr uint64_t NVS_MIN_WRITE_INTERVAL_MS = 5ULL * 60ULL * 1000ULL;

std::mutex g_Mutex;
bool g_Initialized = false;
bool g_RtcAvailable = false;
bool g_RtcBatteryBacked = false;
bool g_HasCurrent = false;
bool g_HasPersisted = false;
TimeSample g_Current = {};
TimeSample g_Persisted = {};
uint32_t g_NvsWriteCount = 0;
bool g_LastNvsWriteValid = false;
uint64_t g_LastNvsWriteMonotonicMs = 0;

uint64_t monotonicMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

bool knownBatteryBackedRtc(m5::board_t board) {
  switch (board) {
    case m5::board_t::board_M5StickC:
    case m5::board_t::board_M5StickCPlus:
    case m5::board_t::board_M5StackCore2:
      return true;
    default:
      return false;
  }
}

void setSystemTime(uint64_t epochUs) {
#if !defined(FURBLE_SIM)
  const timeval value = {
      static_cast<time_t>(epochUs / 1000000ULL),
      static_cast<suseconds_t>(epochUs % 1000000ULL),
  };
  (void)settimeofday(&value, nullptr);
#else
  (void)epochUs;
#endif
}

bool readRtc(TimeSample &sample) {
  if (!g_RtcAvailable) {
    return false;
  }

  m5::rtc_datetime_t datetime;
  if (!M5.Rtc.getDateTime(&datetime)) {
    return false;
  }

  uint64_t epochUs = 0;
  if (!TimeKeeperPolicy::utcToEpochUs(
          static_cast<uint16_t>(datetime.date.year), static_cast<uint8_t>(datetime.date.month),
          static_cast<uint8_t>(datetime.date.date), static_cast<uint8_t>(datetime.time.hours),
          static_cast<uint8_t>(datetime.time.minutes), static_cast<uint8_t>(datetime.time.seconds),
          0, epochUs)) {
    return false;
  }
  sample = {epochUs, g_RtcBatteryBacked ? 2000U : 10U * 60U * 1000U, TimeSource::RTC,
            monotonicMs()};
  return TimeKeeperPolicy::valid(sample);
}

void writeRtc(uint64_t epochUs) {
  if (!g_RtcAvailable) {
    return;
  }

  const time_t seconds = static_cast<time_t>(epochUs / 1000000ULL);
  tm utc = {};
  if (gmtime_r(&seconds, &utc) == nullptr) {
    return;
  }
  const m5::rtc_datetime_t datetime(utc);
  M5.Rtc.setDateTime(&datetime);
}

bool loadPersisted(TimeSample &sample) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true) || !preferences.isKey(NVS_KEY)
      || preferences.getBytesLength(NVS_KEY) == 0) {
    preferences.end();
    return false;
  }
  const size_t length = preferences.getBytesLength(NVS_KEY);
  std::array<uint8_t, 64> buffer = {};
  const bool read =
      length <= buffer.size() && preferences.get(NVS_KEY, buffer.data(), length) == length;
  preferences.end();
  if (!read || !TimeKeeperPolicy::decode(buffer.data(), length, sample)) {
    return false;
  }
  sample.monotonic_ms = monotonicMs();
  // A persisted sample has no reliable power-off duration. Carry an explicit
  // uncertainty penalty so stale records eventually fail closed.
  sample.uncertainty_ms =
      sample.uncertainty_ms
              > TimeKeeperPolicy::MAX_UNCERTAINTY_MS - TimeKeeperPolicy::NVS_RESTORE_UNCERTAINTY_MS
          ? TimeKeeperPolicy::MAX_UNCERTAINTY_MS
          : sample.uncertainty_ms + TimeKeeperPolicy::NVS_RESTORE_UNCERTAINTY_MS;
  return TimeKeeperPolicy::valid(sample);
}

bool savePersisted(const TimeSample &sample, bool force) {
  if (!force && g_LastNvsWriteValid
      && sample.monotonic_ms - g_LastNvsWriteMonotonicMs < NVS_MIN_WRITE_INTERVAL_MS) {
    return false;
  }
  std::array<uint8_t, 64> buffer = {};
  const size_t length = TimeKeeperPolicy::encode(sample, buffer.data(), buffer.size());
  if (length == 0) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    return false;
  }
  const bool ok = preferences.put(NVS_KEY, buffer.data(), length) == length;
  preferences.end();
  if (ok) {
    g_Persisted = sample;
    g_HasPersisted = true;
    g_LastNvsWriteMonotonicMs = sample.monotonic_ms;
    g_LastNvsWriteValid = true;
    g_NvsWriteCount++;
  }
  return ok;
}

}  // namespace

TimeKeeper &TimeKeeper::getInstance() {
  static TimeKeeper instance;
  return instance;
}

void TimeKeeper::init(void) {
  TimeKeeper &keeper = getInstance();
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (g_Initialized) {
    return;
  }

  g_RtcAvailable = M5.Rtc.isEnabled();
  g_RtcBatteryBacked = g_RtcAvailable && knownBatteryBackedRtc(M5.getBoard());

  TimeSample persisted = {};
  g_HasPersisted = loadPersisted(persisted);
  if (g_HasPersisted) {
    g_Persisted = persisted;
  }

  TimeSample rtc = {};
  const bool hasRtc = readRtc(rtc);
  if (hasRtc) {
    g_Current = rtc;
    g_HasCurrent = true;
    setSystemTime(TimeKeeperPolicy::predictedEpochUs(g_Current, monotonicMs()));
  } else if (g_HasPersisted) {
    g_Current = persisted;
    g_HasCurrent = true;
    setSystemTime(TimeKeeperPolicy::predictedEpochUs(g_Current, monotonicMs()));
  }

  g_Initialized = true;
  ESP_LOGI(LOG_TAG, "wall clock: valid=%s rtc=%s battery_backed=%s source=%u",
           g_HasCurrent ? "true" : "false", g_RtcAvailable ? "true" : "false",
           g_RtcBatteryBacked ? "true" : "false",
           g_HasCurrent ? static_cast<unsigned>(g_Current.source) : 0U);
  (void)keeper;
}

bool TimeKeeper::update(TimeSource source, uint64_t epochUs, uint32_t uncertaintyMs) {
  init();
  std::lock_guard<std::mutex> lock(g_Mutex);
  const TimeSample candidate = {epochUs, uncertaintyMs, source, monotonicMs()};
  if (!TimeKeeperPolicy::shouldAccept(g_HasCurrent ? &g_Current : nullptr, candidate,
                                      candidate.monotonic_ms)) {
    return false;
  }
  const bool persist = TimeKeeperPolicy::shouldPersist(g_HasPersisted ? &g_Persisted : nullptr,
                                                       candidate, candidate.monotonic_ms);
  g_Current = candidate;
  g_HasCurrent = true;
  setSystemTime(epochUs);
  writeRtc(epochUs);
  if (persist) {
    (void)savePersisted(candidate, false);
  }
  return true;
}

bool TimeKeeper::updateUtc(TimeSource source,
                           uint16_t year,
                           uint8_t month,
                           uint8_t day,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second,
                           uint8_t centisecond,
                           uint32_t uncertaintyMs) {
  uint64_t epochUs = 0;
  if (!TimeKeeperPolicy::utcToEpochUs(year, month, day, hour, minute, second, centisecond,
                                      epochUs)) {
    return false;
  }
  return update(source, epochUs, uncertaintyMs);
}

TimeKeeper::status_t TimeKeeper::status(void) const {
  std::lock_guard<std::mutex> lock(g_Mutex);
  const uint64_t now = monotonicMs();
  return {g_HasCurrent,
          g_RtcAvailable,
          g_RtcBatteryBacked,
          g_HasCurrent ? TimeKeeperPolicy::predictedEpochUs(g_Current, now) : 0,
          g_HasCurrent ? g_Current.uncertainty_ms : 0,
          g_HasCurrent ? g_Current.source : TimeSource::NONE,
          g_NvsWriteCount};
}

bool TimeKeeper::isValid(void) const {
  std::lock_guard<std::mutex> lock(g_Mutex);
  return g_HasCurrent;
}

uint64_t TimeKeeper::nowEpochUs(void) const {
  std::lock_guard<std::mutex> lock(g_Mutex);
  return g_HasCurrent ? TimeKeeperPolicy::predictedEpochUs(g_Current, monotonicMs()) : 0;
}

void TimeKeeper::flush(void) {
  init();
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!g_HasCurrent) {
    return;
  }
  TimeSample final = g_Current;
  final.epoch_us = TimeKeeperPolicy::predictedEpochUs(g_Current, monotonicMs());
  final.monotonic_ms = monotonicMs();
  if (TimeKeeperPolicy::shouldPersist(g_HasPersisted ? &g_Persisted : nullptr, final,
                                      final.monotonic_ms)) {
    (void)savePersisted(final, true);
  }
}

}  // namespace Furble
