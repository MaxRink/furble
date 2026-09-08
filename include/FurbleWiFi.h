#ifndef FURBLE_WIFI_H
#define FURBLE_WIFI_H

#include <array>
#include <cstdint>
#include <ctime>
#include <string>

#include <Camera.h>

namespace Furble {
class WiFi {
 public:
  enum state_t {
    STATE_DISABLED,
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
  };

  typedef struct {
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
  } status_t;

  WiFi() = delete;
  ~WiFi() = delete;

  /** Initialize the station netif and the WiFi worker. */
  static void init(void);

  /** Start or resume station connection attempts. */
  static bool connect(void);

  /** Stop the station and release its driver resources. */
  static void disconnect(void);

  /** Apply the master WiFi setting. */
  static bool setEnabled(bool enabled);

  /** Forget the saved network and its remembered access point. */
  static void forget(void);

  /** Forget only the remembered access point after a network change. */
  static void clearRememberedAccessPoint(void);

  /** Apply the NTP enable setting. */
  static bool setNtpEnabled(bool enabled);

  /** Reload the configured NTP server if a station is online. */
  static bool reloadNtp(void);

  /** Request an immediate NTP sync. */
  static bool syncNtp(void);

  /** Return a snapshot for the console status commands. */
  static status_t getStatus(void);

  /** Return the current NTP time when a sync has completed. */
  static bool getNtpTimesync(Camera::timesync_t &timesync);
};
}  // namespace Furble

#endif
