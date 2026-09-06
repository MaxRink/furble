#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <cstdint>
#include <mutex>

#include <Camera.h>

namespace Furble {

class GPS {
 public:
  enum source_t {
    SOURCE_NONE,
    SOURCE_UART,
    SOURCE_COMPANION,
  };

  struct external_fix_t {
    Camera::gps_t gps;
    Camera::timesync_t timesync;
    uint32_t age_ms;
    bool position_valid;
    bool time_valid;
    bool altitude_valid;
  };

  static GPS &getInstance();

  GPS(GPS const &) = delete;
  GPS(GPS &&) = delete;
  GPS &operator=(GPS const &) = delete;
  GPS &operator=(GPS &&) = delete;

  bool setExternalFix(const external_fix_t &fix);
  void clearExternalFix(void);
  void reloadSetting(void) {}

  // The production GPS task performs the arbitration and then calls
  // Control::updateGPS. This host hook runs that same downstream handoff after
  // a companion write, allowing the test to observe real camera geodata.
  void update(void);

  /** Highest valid fix hold setting, mirroring the production constant. */
  static constexpr const uint8_t HOLD_MAX = 4;

  source_t getSource(void) const;
  uint8_t getSatellites(void) const;
  external_fix_t getExternalFix(void) const;

 private:
  GPS() = default;

  mutable std::mutex m_Mutex;
  external_fix_t m_ExternalFix = {};
  source_t m_Source = SOURCE_NONE;
  uint8_t m_Satellites = 0;
  bool m_HaveExternalFix = false;
};

}  // namespace Furble

#endif
