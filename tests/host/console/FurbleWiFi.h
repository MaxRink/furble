#ifndef FURBLE_HOST_WIFI_H
#define FURBLE_HOST_WIFI_H

#include <array>
#include <cstdint>
#include <ctime>
#include <string>

#include "Camera.h"

namespace Furble {

class WiFi {
 public:
  enum state_t { STATE_DISABLED, STATE_IDLE, STATE_CONNECTING, STATE_CONNECTED };

  struct status_t {
    bool enabled;
    bool driver;
    state_t state;
    bool connected;
    std::string ssid;
    std::array<uint8_t, 6> bssid;
    bool bssid_set;
    uint8_t channel;
    int8_t rssi;
    std::string ip;
    bool ntp_enabled;
    bool ntp_running;
    bool ntp_synced;
    time_t ntp_last_sync;
    int64_t ntp_offset_us;
  };

  static void init(void);
  static bool connect(void);
  static void disconnect(void);
  static bool setEnabled(bool enabled);
  static void forget(void);
  static void clearRememberedAccessPoint(void);
  static bool setNtpEnabled(bool enabled);
  static bool reloadNtp(void);
  static bool syncNtp(void);
  static status_t getStatus(void);
  static bool getNtpTimesync(Camera::timesync_t &timesync);
};

}  // namespace Furble

#endif
