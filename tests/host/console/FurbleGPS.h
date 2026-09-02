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

  /** Supported standby intervals, in seconds. */
  static constexpr const std::array<uint8_t, 4> DUTY_SECONDS = {0, 5, 10, 15};

  static GPS &getInstance(void);

  bool isEnabled(void) const;
  void reloadSetting(void);
  void reloadLogSettings(void);

  status_t getStatusSnapshot(void) const;
  cycle_status_t getCycleStatusSnapshot(void) const;

  bool sendBinary(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload);
  bool sendAidIni(void);
  std::vector<config_status_t> getConfigStatus(void) const;

  static const char *configStateName(config_state_t state);

 private:
  GPS() = default;
};

}  // namespace Furble

#endif
