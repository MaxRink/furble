// Host TimeKeeper shim for the console command suite.
//
// 'time status' and 'time flush' read and persist a wall clock snapshot. The
// double serves an injectable snapshot and counts the flushes, so both
// subcommands and their argument errors run against the real handler.
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

  static TimeKeeper &getInstance(void);

  status_t status(void) const;
  void flush(void);

 private:
  TimeKeeper() = default;
};

}  // namespace Furble

#endif
