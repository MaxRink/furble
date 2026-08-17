// Host GPS shim for the console command suite.
//
// The real receiver service owns a UART, TinyGPSPlus and a power cycle state
// machine. The console reads a snapshot and sends frames, so the double serves
// an injectable snapshot and records every transmit. That keeps the whole 'gps'
// command tree, including its parsing and error paths, running production code.
#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Furble {

class GPS {
 public:
  enum config_state_t {
    CONFIG_QUEUED,
    CONFIG_SENT,
    CONFIG_ACKED,
    CONFIG_NACKED,
    CONFIG_TIMEOUT,
    CONFIG_FALLBACK,
  };

  typedef struct {
    uint8_t class_id;
    uint8_t message_id;
    config_state_t state;
    uint8_t attempts;
  } config_status_t;

  typedef struct {
    bool fix;
    uint32_t satellites;
    double latitude;
    double longitude;
    double altitude;
    uint32_t location_age;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t chars_processed;
    uint32_t sentences_passed;
  } status_t;

  struct cycle_status_t {
    bool degraded;
    uint32_t retries;
  };

  enum source_t {
    SOURCE_NONE,
    SOURCE_UART,
    SOURCE_COMPANION,
  };

  enum class Fix : uint8_t {
    NONE,
    HELD,
    LIVE,
  };

  struct receiver_status_t {
    const char *cycle_state;
    uint8_t power_policy;
    uint8_t duty_seconds;
    uint16_t rate_ms;
    uint32_t last_sentence_age_ms;
    bool have_sentence;
    uint8_t aid_mode;
    bool aid_cache_valid;
    bool degraded;
    uint32_t retries;
  };

  /** Receiver power policies. */
  static constexpr const uint8_t POWER_ALWAYS_ON = 0;
  static constexpr const uint8_t POWER_STANDBY = 1;
  static constexpr const uint8_t POWER_RAIL_CYCLE = 2;

  /** Supported standby intervals, in seconds. */
  static constexpr const std::array<uint8_t, 4> DUTY_SECONDS = {0, 5, 10, 15};

  /** Highest valid fix hold setting. */
  static constexpr const uint8_t HOLD_MAX = 4;

  static GPS &getInstance(void);

  bool isEnabled(void) const;
  void reloadSetting(void);
  void reloadLogSettings(void);

  status_t getStatusSnapshot(void) const;
  cycle_status_t getCycleStatusSnapshot(void) const;
  receiver_status_t getReceiverStatus(void) const;
  source_t getSource(void) const;
  Fix getFix(void) const;
  uint32_t getHoldLimitMs(void) const;
  uint32_t getHoldRemainingMs(void) const;

  bool sendBinary(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload);
  bool sendAidIni(void);
  std::vector<config_status_t> getConfigStatus(void) const;

  static const char *configStateName(config_state_t state);
  static const char *sourceName(source_t source);

 private:
  GPS() = default;
};

}  // namespace Furble

#endif
