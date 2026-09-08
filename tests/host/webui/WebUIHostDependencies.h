#ifndef FURBLE_HOST_WEBUI_DEPENDENCIES_H
#define FURBLE_HOST_WEBUI_DEPENDENCIES_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define FURBLE_STR "furble"
#define FURBLE_VERSION "host-webui"

using esp_power_level_t = int;

namespace Furble {

class Control {
 public:
  enum cmd_t {
    CMD_SHUTTER_PRESS,
    CMD_SHUTTER_RELEASE,
    CMD_FOCUS_PRESS,
    CMD_FOCUS_RELEASE,
  };
  enum state_t {
    STATE_IDLE,
    STATE_CONNECT,
    STATE_CONNECTING,
    STATE_CONNECT_FAILED,
    STATE_ACTIVE,
    STATE_DISCONNECTING,
  };
  struct command_delivery_t {
    bool any;
    bool all;
  };

  static Control &getInstance() {
    static Control instance;
    return instance;
  }
  state_t getState() const { return state; }
  uint8_t getTargetCount() const { return targets; }
  uint8_t getConnectedTargetCount() const { return connected; }
  command_delivery_t sendCameraCommand(cmd_t command) {
    commands.push_back(command);
    if (beforeDelivery) {
      beforeDelivery(command);
    }
    if (!deliveries.empty()) {
      const auto delivery = deliveries.front();
      deliveries.erase(deliveries.begin());
      return delivery;
    }
    return {true, true};
  }
  void setPower(esp_power_level_t) {}
  void reset() {
    state = STATE_ACTIVE;
    targets = 1;
    connected = 1;
    commands.clear();
    deliveries.clear();
    beforeDelivery = {};
  }

  state_t state = STATE_ACTIVE;
  uint8_t targets = 1;
  uint8_t connected = 1;
  std::vector<cmd_t> commands;
  std::vector<command_delivery_t> deliveries;
  std::function<void(cmd_t)> beforeDelivery;
};

class Settings {
 public:
  enum type_t {
    GPS,
    GPS_BAUD,
    GPS_RATE,
    GPS_NMEA,
    GPS_CONSTEL,
    GPS_POWER,
    GPS_DUTY,
    GPS_ASSIST,
    GPS_HOLD,
    GPS_EXTRAP,
    GPS_PLATFORM,
    GPS_MOTION,
    FB_EVENTS,
    FB_VOLUME,
    TX_POWER,
    COMPANION,
    COMPANION_PASSWORD,
    IMU,
    IMU_WAKE,
    IMU_TRIG,
    WIFI,
    WIFI_SSID,
    WIFI_PSK,
    NTP,
    NTP_SERVER,
    MQTT,
    MQTT_URI,
    MQTT_USER,
    MQTT_PASS,
    MQTT_BASE,
    MQTT_HA,
    WEB_UI,
  };
  struct setting_t {
    type_t type;
    uint8_t wire_id;
    const char *name;
    const char *key;
    const char *nvs_namespace;
  };

  template <type_t Setting>
  static bool load() {
    return Setting == WEB_UI ? webEnabled : false;
  }
  template <typename T>
  static T load(type_t) {
    return T {};
  }
  static bool loadPassword(std::string &value) {
    value = password;
    return passwordLoaded;
  }
  static const std::unordered_map<type_t, setting_t> &all() {
    static const std::unordered_map<type_t, setting_t> settings;
    return settings;
  }
  static const setting_t *getByWireId(uint8_t) { return nullptr; }

  inline static bool webEnabled = true;
  inline static bool passwordLoaded = true;
  inline static std::string password = "correct horse";
};

class CompanionService {
 public:
  enum setting_type_t : uint8_t {
    SETTING_BOOL,
    SETTING_U8,
    SETTING_U32,
    SETTING_STRING,
    SETTING_BLOB,
  };
  static constexpr uint8_t CAMERA_FLAG_CONNECTED = 1U << 3;
  static constexpr int8_t CAMERA_RSSI_UNKNOWN = -128;
  struct camera_record_t {
    uint8_t camera_id = 0;
    uint8_t cam_type = 0;
    uint8_t flags = 0;
    int8_t rssi = CAMERA_RSSI_UNKNOWN;
    uint8_t state = 0;
  };
  struct camera_snapshot_t {
    camera_record_t record;
    std::string name;
  };
  static std::vector<camera_snapshot_t> getCameraSnapshots() { return {}; }
  static setting_type_t settingType(Settings::type_t) { return SETTING_BLOB; }
  static bool settingValue(Settings::type_t, std::vector<uint8_t> &) { return false; }
};

class UI {
 public:
  enum class Request { CONNECT_SAVED, DISCONNECT };
  static bool sendRequest(Request, int32_t) { return true; }
};

class Device {
 public:
  static const std::string getStringID() { return "furble-host"; }
};

class Feedback {
 public:
  static Feedback &getInstance() {
    static Feedback instance;
    return instance;
  }
  void reload() {}
};

class GPS {
 public:
  static GPS &getInstance() {
    static GPS instance;
    return instance;
  }
  void reloadSetting() {}
  void reloadMotionSetting() {}
};

class MQTT {
 public:
  static MQTT &getInstance() {
    static MQTT instance;
    return instance;
  }
  void reloadSetting() {}
};

class CompanionGatt {
 public:
  static CompanionGatt &getInstance() {
    static CompanionGatt instance;
    return instance;
  }
  void reloadSetting() {}
};

class Platform {
 public:
  struct battery_t {
    int level = 80;
    int voltage = 4000;
    int current = 0;
    bool charging = false;
  };
  static Platform &getInstance() {
    static Platform instance;
    return instance;
  }
  battery_t readBattery() { return {}; }
};

class WiFi {
 public:
  struct status_t {
    bool connected = true;
    std::string ip = "192.0.2.10";
    int rssi = -42;
  };
  static status_t getStatus() { return {}; }
  static bool setEnabled(bool) { return true; }
  static void clearRememberedAccessPoint() {}
  static bool setNtpEnabled(bool) { return true; }
  static bool reloadNtp() { return true; }
};

namespace ProvisionTLV {
enum class ValueType : uint8_t { BOOL, U8, U32, STRING, BLOB };
struct SettingValue {
  uint8_t wireId;
  ValueType type;
  std::vector<uint8_t> value;
};
struct ProvisionBundle {
  std::vector<SettingValue> settings;
};
}  // namespace ProvisionTLV

namespace Provision {
enum class ApplyError : uint8_t { NONE };
struct ApplyReport {
  std::string message;
  ApplyError error = ApplyError::NONE;
};
using SettingAppliedCallback = void (*)(uint8_t);
struct ApplyOptions {
  SettingAppliedCallback onSettingApplied = nullptr;
};
inline bool apply(const ProvisionTLV::ProvisionBundle &, ApplyReport &, const ApplyOptions &) {
  return true;
}
inline const char *applyErrorString(ApplyError) { return "apply failed"; }
}  // namespace Provision

}  // namespace Furble

class Preferences {
 public:
  enum class string_result_t { OK, NOT_FOUND };
  bool begin(const char *, bool) { return true; }
  void end() {}
  string_result_t getString(const char *, std::string &) { return string_result_t::NOT_FOUND; }
  bool putString(const char *, const char *) { return true; }
};

#endif
