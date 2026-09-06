#ifndef SETTINGS_H
#define SETTINGS_H

#include <unordered_map>

#include "Preferences.h"

#include "FurbleTextSize.h"
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
    COMPANION_PASSWORD,
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
#if !defined(FURBLE_NO_DISPLAY)
    DISPLAY_MODE,
#endif
    BATTERY_SAVER,
#if defined(FURBLE_M5STICKS3)
    WATCHDOG,
#endif
  } type_t;

#if !defined(FURBLE_NO_DISPLAY)
  typedef enum {
    GUI = 0,
    CONSOLE = 1,
  } display_mode_t;
#endif

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

  static constexpr size_t MULTISELECT_MAX = 8;
  // A remembered entry is the camera's displayed name, and a vendor client may
  // compose that name rather than show the bare advertised one. A Fujifilm
  // Secure body renders "MODEL SERIAL", and the serial falls back to ten hex
  // characters when it is not printable text. Sixteen bytes truncated that, and
  // a truncated name cannot tell two bodies of one model apart. Thirty-two
  // holds a twenty-one character model, a space, and the ten character
  // fallback.
  static constexpr size_t MULTISELECT_NAME_MAX = 32;
  /** The field width used before composed vendor names needed the extra room. */
  static constexpr size_t MULTISELECT_NAME_LEGACY_MAX = 16;

  typedef struct {
    char name[MULTISELECT_MAX][MULTISELECT_NAME_MAX];
    uint8_t count;
  } multiselect_t;

  /** The record layout written before MULTISELECT_NAME_MAX widened. */
  typedef struct {
    char name[MULTISELECT_MAX][MULTISELECT_NAME_LEGACY_MAX];
    uint8_t count;
  } multiselect_legacy_t;

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

  static constexpr uint32_t BAUD_AUTO = 0;
  static constexpr uint32_t BAUD_9600 = 9600;
  static constexpr uint32_t BAUD_115200 = 115200;

  /** Default maximum CPU frequency in MHz, matches Platform. */
  static constexpr uint8_t CPU_FREQ_DEFAULT = 160;

  static constexpr SpinValue::nvs_t BULB_DEFAULT = {30, SpinValue::UNIT_SEC};

  static constexpr const char *BUTTON_MODE_TWO_BUTTON_VALUE = "two-button";
  static constexpr const char *BUTTON_MODE_ONE_BUTTON_VALUE = "one-button";

  static constexpr uint16_t GPX_PERIOD_MIN = 1;
  static constexpr uint16_t GPX_PERIOD_MAX = 60;
  static constexpr uint16_t GPX_PERIOD_DEFAULT = 5;

  /** Clamp a GPX period to the valid range, falling back to the default. */
  static constexpr uint16_t clampGPXPeriod(uint16_t seconds) {
    return ((seconds >= GPX_PERIOD_MIN) && (seconds <= GPX_PERIOD_MAX)) ? seconds
                                                                        : GPX_PERIOD_DEFAULT;
  }

  static void init(void);

  static const setting_t &get(type_t);
  static const setting_t *getByWireId(uint8_t wire_id);
  static const std::unordered_map<type_t, setting_t> &all(void);

  /** Return true when a saved value takes effect without a reboot. */
  static bool appliesImmediately(type_t type);

  /** Return true when an over-the-air write can affect the companion link. */
  static bool isDangerous(type_t type);

  /** Return true when the Battery Saver power profile is enabled. */
  static bool batterySaver(void);

  /**
   * Remember one camera name in a multi-connect selection.
   *
   * Returns false when the set is full, the name is missing, or the name does
   * not fit the field. A truncated entry would compare equal to a different
   * camera whose whole name is that prefix, so a name that does not fit is
   * refused rather than shortened: the set forgets one body instead of ticking
   * another one.
   */
  static bool multiselectAdd(multiselect_t &selection, const char *name);

  /**
   * Return true when a camera name is in a remembered multi-connect set.
   *
   * The comparison is over the whole stored string. A prefix comparison would
   * tick a different body whose displayed name agrees only up to the compared
   * length, which composed "model serial" names make reachable.
   */
  static bool multiselectContains(const multiselect_t &selection, const char *name);

  /** Widen a record written in the layout that preceded MULTISELECT_NAME_MAX. */
  static multiselect_t multiselectFromLegacy(const multiselect_legacy_t &legacy);

  // Effective power-setting accessors. When Battery Saver is on, each returns
  // the battery-optimal value from the bundle, otherwise the user's stored
  // value. The stored individual settings are never modified, so turning the
  // profile off restores the user's own choices with no bookkeeping. Read
  // these instead of the raw setting anywhere the value drives power behavior.
  static bool sleepConnEffective(void);
  static bool connSaverEffective(void);
  static bool reconBackoffEffective(void);
  static uint8_t scanModeEffective(void);
  static uint8_t inactivityEffective(void);
  static uint8_t displayOffEffective(void);

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
struct Settings::storage_type<Settings::IMU> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::IMU_WAKE> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::IMU_TRIG> {
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
struct Settings::storage_type<Settings::GPS_ASSIST> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::GPS_HOLD> {
  using type = uint8_t;
};
template <>
struct Settings::storage_type<Settings::GPS_EXTRAP> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::GPS_PLATFORM> {
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
struct Settings::storage_type<Settings::MULTISELECT> {
  using type = Settings::multiselect_t;
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
struct Settings::storage_type<Settings::COMPANION_PASSWORD> {
  using type = std::string;
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
template <>
struct Settings::storage_type<Settings::AUTO_OFF_CHARGING> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::SD_GPX> {
  using type = bool;
};
template <>
struct Settings::storage_type<Settings::GPX_PERIOD> {
  using type = uint16_t;
};
template <>
struct Settings::storage_type<Settings::BOOT_SPLASH> {
  using type = bool;
};
#if !defined(FURBLE_NO_DISPLAY)
template <>
struct Settings::storage_type<Settings::DISPLAY_MODE> {
  using type = uint8_t;
};
#endif
template <>
struct Settings::storage_type<Settings::BATTERY_SAVER> {
  using type = bool;
};
#if defined(FURBLE_M5STICKS3)
template <>
struct Settings::storage_type<Settings::WATCHDOG> {
  using type = bool;
};
#endif

}  // namespace Furble

#endif
