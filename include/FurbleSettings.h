#ifndef SETTINGS_H
#define SETTINGS_H

#include <unordered_map>

#include "Preferences.h"

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
    GPS_BAUD,
    GPS_RATE,
    GPS_NMEA,
    GPS_CONSTEL,
    GPS_POWER,
    GPS_DUTY,
    INTERVAL,
    MULTICONNECT,
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
#if defined(FURBLE_M5STICKS3)
    WATCHDOG,
#endif
  } type_t;

  typedef struct {
    union {
      struct {
        uint16_t x;
        uint16_t y;
      };
      uint16_t coords[2];
    };
  } calibration_point_t;

  typedef struct {
    union {
      struct {
        calibration_point_t top_left;
        calibration_point_t bottom_left;
        calibration_point_t top_right;
        calibration_point_t bottom_right;
      };
      calibration_point_t pairs[4];
      uint16_t points[8];
    };
    bool calibrated;
  } calibration_t;

  typedef struct {
    type_t type;
    uint8_t wire_id;
    const char *name;
    const char *key;
    const char *nvs_namespace;
  } setting_t;

  /** Battery status display styles. */
  typedef enum {
    BATT_STYLE_ICON = 0,
    BATT_STYLE_PERCENT = 1,
    BATT_STYLE_BOTH = 2,
  } batt_style_t;

  /** UI text size choices. */
  typedef enum {
    TEXT_SIZE_SMALL = 0,
    TEXT_SIZE_NORMAL = 1,
    TEXT_SIZE_LARGE = 2,
  } text_size_t;

  /** Main button behavior modes. */
  typedef enum {
    BUTTON_MODE_TWO_BUTTON = 0,
    BUTTON_MODE_ONE_BUTTON = 1,
  } button_mode_t;

  static constexpr uint32_t BAUD_9600 = 9600;
  static constexpr uint32_t BAUD_115200 = 115200;

  /** Default maximum CPU frequency in MHz, matches Platform. */
  static constexpr uint8_t CPU_FREQ_DEFAULT = 160;

  static constexpr SpinValue::nvs_t BULB_DEFAULT = {30, SpinValue::UNIT_SEC};

  static constexpr const char *BUTTON_MODE_TWO_BUTTON_VALUE = "two-button";
  static constexpr const char *BUTTON_MODE_ONE_BUTTON_VALUE = "one-button";

  static void init(void);

  static const setting_t &get(type_t);
  static const setting_t *getByWireId(uint8_t wire_id);
  static const std::unordered_map<type_t, setting_t> &all(void);

  /** Retrieve every setting, keyed by type. */
  static const std::unordered_map<type_t, setting_t> &getAll(void) { return m_Setting; }

  /** Bind each setting to its storage type for type-safe load/save. */
  template <type_t S>
  struct storage_type;

  /** Load a setting, with type deduced from the setting. */
  template <type_t S>
  static typename storage_type<S>::type load() {
    return load<typename storage_type<S>::type>(S);
  }

  /** Save a setting, with type deduced from the setting. */
  template <type_t S>
  static void save(const typename storage_type<S>::type &value) {
    save<typename storage_type<S>::type>(S, value);
  }

  template <typename T>
  static T load(type_t type);

  template <typename T>
  static void save(const type_t type, const T &value);

 private:
  template <typename T>
  static T loadValue(type_t type);
  template <typename T>
  static void saveValue(type_t type, const T &value);

  static const std::unordered_map<type_t, setting_t> m_Setting;
  static Preferences m_Prefs;
};

template <>
struct Settings::storage_type<Settings::BRIGHTNESS> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::INACTIVITY> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::DISPLAY_OFF> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::THEME> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::TEXT_SIZE> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::TX_POWER> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::TX_ADAPTIVE> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::GPS> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::GPS_BAUD> {
  using type = uint32_t;
};
template <>
struct Settings::storage_type<Settings::GPS_RATE> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::GPS_NMEA> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::GPS_CONSTEL> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::GPS_POWER> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::GPS_DUTY> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::INTERVAL> {
  using type = interval_t;
};
template <>
struct Settings::storage_type<Settings::MULTICONNECT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::RECONNECT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::RECON_BACKOFF> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::FAUXNY> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::TOUCH_CALIBRATION> {
  using type = Settings::calibration_t;
};
template <>
struct Settings::storage_type<Settings::AUTOCONNECT> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::CPU_FREQ> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::BATT_STYLE> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::SHOW_TITLE> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::SLEEP_CONN> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::BULB> {
  using type = SpinValue::nvs_t;
};
template <>
struct Settings::storage_type<Settings::SCAN_MODE> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::SCAN_TIMEOUT> {
  using type = uint32_t;
};
template <>
struct Settings::storage_type<Settings::COMPANION> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::CONN_SAVER> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::IR> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::IR_PROTO> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::FB_OUTPUT> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::FB_EVENTS> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::FB_VOLUME> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::PRESET_PICKER> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::BUTTON_MODE> {
  using type = std::string;
};
template <>
struct Settings::storage_type<Settings::AUTO_OFF> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::LOW_BATT> {
  using type = uint8_t;
};
#if defined(FURBLE_M5STICKS3)
template <>
struct Settings::storage_type<Settings::WATCHDOG> {
  using type = bool;
};
#endif

}  // namespace Furble

#endif
