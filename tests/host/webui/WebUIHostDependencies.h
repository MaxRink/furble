#ifndef FURBLE_HOST_WEBUI_DEPENDENCIES_H
#define FURBLE_HOST_WEBUI_DEPENDENCIES_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define FURBLE_STR "furble"
#define FURBLE_VERSION "host-webui"

using esp_power_level_t = int;

namespace Furble {

class Camera {
 public:
  struct address_t {
    operator uint64_t() const { return 0; }
  };
  bool isConnected() const { return true; }
  address_t getAddress() const { return {}; }
};

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
    bool connected_all = false;
    uint32_t session = 0;
    std::vector<uint64_t> accepted;
  };

  static Control &getInstance() {
    static Control instance;
    return instance;
  }
  state_t getState() const { return state; }
  uint8_t getTargetCount() const { return targets; }
  std::vector<std::shared_ptr<Camera>> getTargetCameras() const { return {}; }
  uint8_t getConnectedTargetCount() const { return connected; }
  uint32_t getSessionGeneration() const { return session; }
  command_delivery_t sendCameraCommand(cmd_t command,
                                       uint32_t expectedSession = UINT32_MAX,
                                       const std::vector<uint64_t> *only = nullptr) {
    (void)only;
    if ((expectedSession != UINT32_MAX) && (expectedSession != session)) {
      return {false, false, false, session, {}};
    }
    commands.push_back(command);
    if (beforeDelivery) {
      beforeDelivery(command);
    }
    if (!deliveries.empty()) {
      const auto delivery = deliveries.front();
      deliveries.erase(deliveries.begin());
      return {delivery.any, delivery.all, delivery.connected_all, session, delivery.accepted};
    }
    return {true, true, true, session, {1}};
  }
  void setPower(esp_power_level_t) {}
  void reset() {
    state = STATE_ACTIVE;
    targets = 1;
    connected = 1;
    session = 1;
    commands.clear();
    deliveries.clear();
    beforeDelivery = {};
  }

  state_t state = STATE_ACTIVE;
  uint8_t targets = 1;
  uint8_t connected = 1;
  uint32_t session = 1;
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
    BRIGHTNESS,
    THEME,
    IR,
    AUTO_OFF,
    LOW_BATT,
    AUTO_OFF_CHARGING,
    SD_GPX,
    GPX_PERIOD,
    DISPLAY_MODE,
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
    static const std::unordered_map<type_t, setting_t> settings = {
        {BRIGHTNESS,         {BRIGHTNESS, 20, "Brightness", "brightness", "furble"}    },
        {COMPANION_PASSWORD,
         {COMPANION_PASSWORD, 40, "Companion Password", "companion_password", "furble"}},
        {THEME,              {THEME, 61, "Theme", "theme", "furble"}                   },
        {WEB_UI,             {WEB_UI, 62, "Web UI", "web_ui", "furble"}                },
    };
    return settings;
  }
  static const setting_t *getByWireId(uint8_t wire_id) {
    for (const auto &entry : all()) {
      if (entry.second.wire_id == wire_id) {
        return &entry.second;
      }
    }
    return nullptr;
  }

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
  static setting_type_t settingType(Settings::type_t type) {
    switch (type) {
      case Settings::BRIGHTNESS:
        return SETTING_U8;
      case Settings::WEB_UI:
        return SETTING_BOOL;
      case Settings::THEME:
      case Settings::COMPANION_PASSWORD:
        return SETTING_STRING;
      default:
        return SETTING_BLOB;
    }
  }
  static bool settingValue(Settings::type_t type, std::vector<uint8_t> &value) {
    switch (type) {
      case Settings::BRIGHTNESS:
        value = {77};
        return true;
      case Settings::WEB_UI:
        value = {1};
        return true;
      case Settings::THEME:
        value = {'D', 'a', 'r', 'k'};
        return true;
      default:
        return false;
    }
  }
};

class UI {
 public:
  enum class Request {
    CONNECT_SAVED,
    DISCONNECT,
    GPS_RELOAD,
    FEEDBACK_RELOAD,
    IR_RELOAD,
    POWER_RELOAD,
    SD_RELOAD,
    DISPLAY_MODE
  };
  static bool sendRequest(Request, int32_t) { return true; }
  static void notifyGestureSettingsChanged() {}
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
  struct battery_sample_t {
    battery_t battery;
  };
  static Platform &getInstance() {
    static Platform instance;
    return instance;
  }
  battery_t readBattery() const { return {}; }
  battery_sample_t getBatterySample() const { return {readBattery()}; }
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
inline const char *applyErrorString(ApplyError) {
  return "apply failed";
}
}  // namespace Provision

}  // namespace Furble

class Preferences {
 public:
  enum class string_result_t { OK, NOT_FOUND, ERROR };
  bool begin(const char *, bool readOnly) {
    if (openFail) {
      return false;
    }
    return readOnly || !writableOpenFail;
  }
  void end() {}
  string_result_t getString(const char *key, std::string &value) {
    if (readFail) {
      return string_result_t::ERROR;
    }
    const auto found = values.find(key);
    if (found == values.end()) {
      return string_result_t::NOT_FOUND;
    }
    value = found->second;
    return string_result_t::OK;
  }
  bool putString(const char *key, const char *value) {
    if (writeFail) {
      return false;
    }
    values[key] = value;
    return true;
  }

  static void reset() {
    openFail = false;
    writableOpenFail = false;
    readFail = false;
    writeFail = false;
    values.clear();
  }

  inline static bool openFail = false;
  inline static bool writableOpenFail = false;
  inline static bool readFail = false;
  inline static bool writeFail = false;
  inline static std::map<std::string, std::string> values;
};

#endif
