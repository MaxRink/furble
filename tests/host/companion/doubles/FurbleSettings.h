#ifndef SETTINGS_H
#define SETTINGS_H

// Host-only Settings surface for the companion service test. It keeps the
// production wire metadata and type names visible to FurbleCompanionService,
// while the values live in typed in-memory maps instead of NVS.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "interval.h"

namespace Furble {

class Settings {
 public:
  Settings() = delete;
  ~Settings() = delete;

  typedef enum {
    BRIGHTNESS,
    INACTIVITY,
    DISPLAY_OFF,
    THEME,
    TEXT_SIZE,
    TX_POWER,
    TX_ADAPTIVE,
    GPS,
    IMU,
    IMU_WAKE,
    IMU_TRIG,
    HW_MOTION,
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
    INTERVAL,
    MULTICONNECT,
    MULTISELECT,
    RECONNECT,
    RECON_BACKOFF,
    FAUXNY,
    TOUCH_CALIBRATION,
    AUTOCONNECT,
    CPU_FREQ,
    BATT_STYLE,
    SHOW_TITLE,
    SLEEP_CONN,
    BULB,
    SCAN_MODE,
    SCAN_TIMEOUT,
    COMPANION,
    CONN_SAVER,
    IR,
    IR_PROTO,
    FB_OUTPUT,
    FB_EVENTS,
    FB_VOLUME,
    PRESET_PICKER,
    BUTTON_MODE,
    AUTO_OFF,
    LOW_BATT,
    AUTO_OFF_CHARGING,
    SD_GPX,
    GPX_PERIOD,
    BOOT_SPLASH,
    BATTERY_SAVER,
  } type_t;

  typedef struct {
    type_t type;
    uint8_t wire_id;
    const char *name;
    const char *key;
    const char *nvs_namespace;
  } setting_t;

  static constexpr const char *BUTTON_MODE_TWO_BUTTON_VALUE = "two-button";
  static constexpr const char *BUTTON_MODE_ONE_BUTTON_VALUE = "one-button";
  static constexpr uint32_t BAUD_AUTO = 0;
  static constexpr uint32_t BAUD_9600 = 9600;
  static constexpr uint32_t BAUD_115200 = 115200;

  typedef enum {
    HW_MOTION_AUTO = 0,
    HW_MOTION_SOFTWARE = 1,
    HW_MOTION_HARDWARE = 2,
  } hw_motion_t;

  static void init(void) {}

  static const setting_t &get(type_t type);
  static const setting_t *getByWireId(uint8_t wire_id);
  static const std::unordered_map<type_t, setting_t> &all(void);
  static bool appliesImmediately(type_t type);
  static bool isDangerous(type_t type);

  template <type_t S>
  struct storage_type;

  template <type_t S>
  static typename storage_type<S>::type load(void) {
    return load<typename storage_type<S>::type>(S);
  }

  template <typename T>
  static T load(type_t type) {
    const auto &values = typedValues<T>();
    const auto found = values.find(type);
    return found == values.end() ? defaultValue<T>() : found->second;
  }

  template <typename T>
  static void save(type_t type, const T &value) {
    typedValues<T>()[type] = value;
  }

  static bool sleepConnEffective(void) { return load<bool>(SLEEP_CONN); }
  static bool connSaverEffective(void) { return load<bool>(CONN_SAVER); }
  static bool reconBackoffEffective(void) { return load<bool>(RECON_BACKOFF); }

  // Scenario setup helpers. The assertions read the same Settings::load path
  // that CompanionService::saveSetting writes.
  static void setBool(type_t type, bool value) { save<bool>(type, value); }
  static void setU8(type_t type, uint8_t value) { save<uint8_t>(type, value); }

 private:
  template <typename T>
  static std::unordered_map<type_t, T> &typedValues(void) {
    static std::unordered_map<type_t, T> values;
    return values;
  }

  template <typename T>
  static T defaultValue(void) {
    return T {};
  }
};

}  // namespace Furble

template <>
struct Furble::Settings::storage_type<Furble::Settings::TX_ADAPTIVE> {
  using type = bool;
};

template <>
struct Furble::Settings::storage_type<Furble::Settings::GPS_PLATFORM> {
  using type = uint8_t;
};

template <>
struct Furble::Settings::storage_type<Furble::Settings::IMU_WAKE> {
  using type = uint8_t;
};

template <>
struct Furble::Settings::storage_type<Furble::Settings::IMU_TRIG> {
  using type = bool;
};

struct Furble::Settings::storage_type<Furble::Settings::HW_MOTION> {
  using type = uint8_t;
};

#endif
