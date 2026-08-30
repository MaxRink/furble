#ifndef FURBLE_HOST_MQTT_DEPENDENCIES_H
#define FURBLE_HOST_MQTT_DEPENDENCIES_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include "Camera.h"
#include "interval.h"

namespace Furble {

class Control {
 public:
  enum cmd_t {
    CMD_SHUTTER_PRESS,
    CMD_SHUTTER_RELEASE,
    CMD_FOCUS_PRESS,
    CMD_FOCUS_RELEASE,
    CMD_GPS_UPDATE,
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_ERROR,
  };

  enum state_t {
    STATE_IDLE,
    STATE_CONNECT,
    STATE_CONNECTING,
    STATE_CONNECT_FAILED,
    STATE_ACTIVE,
    STATE_DISCONNECTING,
  };

  struct target_status_t {
    std::string id;
    std::string name;
    Camera::Type type;
    bool connected;
    uint8_t progress;
    int16_t rssi;
  };

  static Control &getInstance(void);

  state_t getState(void) const { return m_State; }
  BaseType_t sendCommand(cmd_t command);
  std::vector<target_status_t> getTargetStatus(void) const { return m_Targets; }

  static std::string getCameraID(const Camera &camera) { return camera.getID(); }
  void addActive(const std::shared_ptr<Camera> &) {}
  void connectAll(bool) {}
  void disconnect(void) {}

  static void reset(void);
  static void setState(state_t state);
  static const std::vector<cmd_t> &commands(void);
  static void setTargetStatus(const std::vector<target_status_t> &targets);

 private:
  state_t m_State = STATE_ACTIVE;
  std::vector<target_status_t> m_Targets;
  inline static std::vector<cmd_t> m_Commands;
};

class Platform {
 public:
  struct battery_t {
    uint8_t level = 0;
    uint16_t voltage = 0;
    int32_t current = 0;
    bool charging = false;
  };

  static Platform &getInstance(void);
  battery_t readBattery(void) const { return m_Battery; }
  static void setBattery(const battery_t &battery);

 private:
  battery_t m_Battery;
};

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
    uint32_t age_ms = 0;
    float accuracy_m = 0;
    bool position_valid = false;
    bool time_valid = false;
    bool altitude_valid = false;
    bool accuracy_valid = false;
  };

  static GPS &getInstance(void);
  bool getCurrentFix(external_fix_t &fix) const;
  source_t getSource(void) const { return m_Source; }
  bool setExternalFix(const external_fix_t &fix) {
    m_Fix = fix;
    m_HaveFix = true;
    m_Source = SOURCE_COMPANION;
    return true;
  }
  static void reset(void);

 private:
  external_fix_t m_Fix;
  source_t m_Source = SOURCE_NONE;
  bool m_HaveFix = false;
};

class Settings {
 public:
  enum type_t {
    GPS,
    INTERVAL,
    RECONNECT,
    MQTT,
    MQTT_URI,
    MQTT_USER,
    MQTT_PASS,
    MQTT_BASE,
    MQTT_HA,
  };

  template <type_t S>
  struct storage_type;

  template <type_t S>
  static typename storage_type<S>::type load() {
    return {};
  }

  static void reset(void);
  static bool mqttEnabled;
  static bool mqttHA;
  static bool gpsEnabled;
  static bool reconnect;
  static std::string mqttURI;
  static std::string mqttUser;
  static std::string mqttPassword;
  static std::string mqttBase;
  static interval_t interval;
};

template <>
struct Settings::storage_type<Settings::GPS> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::INTERVAL> {
  using type = interval_t;
};
template <>
struct Settings::storage_type<Settings::RECONNECT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::MQTT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::MQTT_URI> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::MQTT_USER> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::MQTT_PASS> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::MQTT_BASE> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::MQTT_HA> {
  using type = bool;
};

template <>
inline bool Settings::load<Settings::GPS>() {
  return gpsEnabled;
}
template <>
inline interval_t Settings::load<Settings::INTERVAL>() {
  return interval;
}
template <>
inline bool Settings::load<Settings::RECONNECT>() {
  return reconnect;
}
template <>
inline bool Settings::load<Settings::MQTT>() {
  return mqttEnabled;
}
template <>
inline std::string Settings::load<Settings::MQTT_URI>() {
  return mqttURI;
}
template <>
inline std::string Settings::load<Settings::MQTT_USER>() {
  return mqttUser;
}
template <>
inline std::string Settings::load<Settings::MQTT_PASS>() {
  return mqttPassword;
}
template <>
inline std::string Settings::load<Settings::MQTT_BASE>() {
  return mqttBase;
}
template <>
inline bool Settings::load<Settings::MQTT_HA>() {
  return mqttHA;
}

}  // namespace Furble

#endif
