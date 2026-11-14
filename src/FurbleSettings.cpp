#include <esp_bt.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FurbleBatterySaver.h"
#include "FurbleSettings.h"
#include "FurbleSettingsBootstrap.h"
#include "FurbleSettingsDefaults.h"
#include "FurbleTimezone.h"
#include "FurbleTypes.h"
#include "Preferences.h"

namespace Furble {

// The board-conditional text size policy in FurbleTextSize.h uses raw values so
// it stays dependency free for the host tests. Pin those values to the enum so
// the two cannot drift apart.
static_assert(TextSizePolicy::SMALL == Settings::TEXT_SIZE_SMALL,
              "text size policy Small must match the enum");
static_assert(TextSizePolicy::NORMAL == Settings::TEXT_SIZE_NORMAL,
              "text size policy Normal must match the enum");
static_assert(TextSizePolicy::LARGE == Settings::TEXT_SIZE_LARGE,
              "text size policy Large must match the enum");

const std::unordered_map<Settings::type_t, Settings::setting_t> Settings::m_Setting = {
    {BRIGHTNESS,         {BRIGHTNESS, 1, "Brightness", "brightness", "M5ez"}                  },
    {INACTIVITY,         {INACTIVITY, 2, "Inactivity", "inactivity", "M5ez"}                  },
    {DISPLAY_OFF,        {DISPLAY_OFF, 24, "Screen off", "display_off", FURBLE_STR}           },
    {THEME,              {THEME, 3, "Theme", "theme", "M5ez"}                                 },
    {TEXT_SIZE,          {TEXT_SIZE, 40, "Text size", "text_size", FURBLE_STR}                },
    {TX_POWER,           {TX_POWER, 4, "TX Power", "tx_power", FURBLE_STR}                    },
    {LEGEND,             {LEGEND, 65, "Legend", "legend", FURBLE_STR}                         },
    {TX_ADAPTIVE,        {TX_ADAPTIVE, 28, "Adaptive", "tx_adaptive", FURBLE_STR}             },
    {GPS,                {GPS, 5, "GPS", "gps", FURBLE_STR}                                   },
    {IMU,                {IMU, 46, "IMU", "imu", FURBLE_STR}                                  },
    {IMU_WAKE,           {IMU_WAKE, 72, "Wake Gesture", "imu_wake", FURBLE_STR}               },
    {IMU_TRIG,           {IMU_TRIG, 73, "Double-Tap Shutter", "imu_trigger", FURBLE_STR}      },
    {HW_MOTION,          {HW_MOTION, 74, "Motion Engine", "hw_motion", FURBLE_STR}            },
    {GPS_BAUD,           {GPS_BAUD, 6, "GPS Baud", "gps_baud", FURBLE_STR}                    },
    {GPS_RATE,           {GPS_RATE, 13, "GPS Rate", "gps_rate", FURBLE_STR}                   },
    {GPS_NMEA,           {GPS_NMEA, 14, "GPS Sentences", "gps_nmea", FURBLE_STR}              },
    {GPS_CONSTEL,        {GPS_CONSTEL, 15, "GPS Constellation", "gps_constel", FURBLE_STR}    },
    {GPS_POWER,          {GPS_POWER, 25, "GPS Power", "gps_power", FURBLE_STR}                },
    {GPS_DUTY,           {GPS_DUTY, 26, "GPS Duty", "gps_duty", FURBLE_STR}                   },
    {GPS_ASSIST,         {GPS_ASSIST, 41, "GPS Assistance", "gps_assist", FURBLE_STR}         },
    {GPS_MOTION,         {GPS_MOTION, 66, "Motion Adaptive", "gps_motion", FURBLE_STR}        },
    {GPS_HOLD,           {GPS_HOLD, 67, "Fix Hold", "gps_hold", FURBLE_STR}                   },
    {GPS_EXTRAP,         {GPS_EXTRAP, 68, "Extrapolate", "gps_extrap", FURBLE_STR}            },
    {GPS_PLATFORM,       {GPS_PLATFORM, 69, "GPS Platform", "gps_plat", FURBLE_STR}           },
    {GPS_GNSS,           {GPS_GNSS, 48, "GPS Signals", "gps_gnss", FURBLE_STR}                },
    {GPS_PSM,            {GPS_PSM, 49, "GPS Power Save", "gps_psm", FURBLE_STR}               },
    {GPS_ANA,            {GPS_ANA, 50, "GPS AssistNow", "gps_ana", FURBLE_STR}                },
    {INTERVAL,           {INTERVAL, 7, "Interval", "interval", FURBLE_STR}                    },
    {MULTICONNECT,       {MULTICONNECT, 8, "Multi-Connect", "multiconnect", FURBLE_STR}       },
    {MULTISELECT,        {MULTISELECT, 0, "Multi-Select", "multiselect", FURBLE_STR}          },
    {RECONNECT,          {RECONNECT, 9, "Infinite-ReConnect", "reconnect", FURBLE_STR}        },
    {RECON_BACKOFF,      {RECON_BACKOFF, 16, "Reconnect Backoff", "recon_backoff", FURBLE_STR}},
    {FAUXNY,             {FAUXNY, 10, "FauxNY", "fauxNY", FURBLE_STR}                         },
    {TOUCH_CALIBRATION,  {TOUCH_CALIBRATION, 0, "Touch Calibration", "t_calib", FURBLE_STR}   },
    {AUTOCONNECT,        {AUTOCONNECT, 11, "Auto-Connect", "autoconnect", FURBLE_STR}         },
    {COMPANION,          {COMPANION, 12, "Companion", "companion", FURBLE_STR}                },
    {COMPANION_PASSWORD,
     {COMPANION_PASSWORD, 47, "Companion password", "companion_pw", FURBLE_STR}               },
    {CPU_FREQ,           {CPU_FREQ, 17, "CPU Speed", "cpu_freq", FURBLE_STR}                  },
    {BATT_STYLE,         {BATT_STYLE, 18, "Battery Style", "batt_style", FURBLE_STR}          },
    {SHOW_TITLE,         {SHOW_TITLE, 19, "Show Title", "show_title", FURBLE_STR}             },
    {SLEEP_CONN,         {SLEEP_CONN, 20, "Sleep while connected", "sleep_conn", FURBLE_STR}  },
    {BULB,               {BULB, 0, "Bulb", "bulb", FURBLE_STR}                                },
    {SCAN_MODE,          {SCAN_MODE, 21, "Scan Mode", "scan_mode", FURBLE_STR}                },
    {SCAN_TIMEOUT,       {SCAN_TIMEOUT, 22, "Scan Timeout", "scan_timeout", FURBLE_STR}       },
    {CONN_SAVER,         {CONN_SAVER, 29, "Connection power save", "conn_saver", FURBLE_STR}  },
    {IR,                 {IR, 31, "Infrared", "ir", FURBLE_STR}                               },
    {IR_PROTO,           {IR_PROTO, 32, "IR Protocol", "ir_proto", FURBLE_STR}                },
    {FB_OUTPUT,          {FB_OUTPUT, 33, "Feedback", "fb_output", FURBLE_STR}                 },
    {FB_EVENTS,          {FB_EVENTS, 34, "Feedback Events", "fb_events", FURBLE_STR}          },
    {FB_VOLUME,          {FB_VOLUME, 35, "Volume", "fb_volume", FURBLE_STR}                   },
    {PRESET_PICKER,      {PRESET_PICKER, 30, "Preset Picker", "preset_picker", FURBLE_STR}    },
    {BUTTON_MODE,        {BUTTON_MODE, 27, "Button Mode", "button_mode", FURBLE_STR}          },
    {AUTO_OFF,           {AUTO_OFF, 37, "Auto off", "auto_off", FURBLE_STR}                   },
    {LOW_BATT,           {LOW_BATT, 38, "Low battery", "low_batt", FURBLE_STR}                },
    {AUTO_OFF_CHARGING,
     {AUTO_OFF_CHARGING, 43, "Auto off while charging", "autooff_charge", FURBLE_STR}         },
    {SD_GPX,             {SD_GPX, 39, "GPX Logging", "sd_gpx", FURBLE_STR}                    },
    {GPX_PERIOD,         {GPX_PERIOD, 0, "GPX Interval", "gpx_period", FURBLE_STR}            },
    {BOOT_SPLASH,        {BOOT_SPLASH, 44, "Boot screen", "boot_splash", FURBLE_STR}          },
#if defined(FURBLE_MQTT) && FURBLE_MQTT
    {MQTT,               {MQTT, 56, "MQTT", "mqtt", FURBLE_STR}                               },
    {MQTT_URI,           {MQTT_URI, 57, "MQTT URI", "mqtt_uri", FURBLE_STR}                   },
    {MQTT_USER,          {MQTT_USER, 58, "MQTT User", "mqtt_user", FURBLE_STR}                },
    {MQTT_PASS,          {MQTT_PASS, 59, "MQTT Password", "mqtt_pass", FURBLE_STR}            },
    {MQTT_BASE,          {MQTT_BASE, 60, "MQTT Base", "mqtt_base", FURBLE_STR}                },
    {MQTT_HA,            {MQTT_HA, 61, "MQTT Home Assistant", "mqtt_ha", FURBLE_STR}          },
#endif
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
    {WEB_UI,             {WEB_UI, 62, "WebUI", "web_ui", FURBLE_STR}                          },
#endif
#if !defined(FURBLE_NO_DISPLAY)
    {DISPLAY_MODE,       {DISPLAY_MODE, 36, "Display Mode", "display_mode", FURBLE_STR}       },
#endif
    {BATTERY_SAVER,      {BATTERY_SAVER, 0, "Battery Saver", "batt_saver", FURBLE_STR}        },
#if defined(FURBLE_M5STICKS3)
    {WATCHDOG,           {WATCHDOG, 23, "Watchdog", "watchdog", FURBLE_STR}                   },
#endif
    {WIFI,               {WIFI, 51, "WiFi", "wifi", FURBLE_STR}                               },
    {WIFI_SSID,          {WIFI_SSID, 52, "WiFi SSID", "wifi_ssid", FURBLE_STR}                },
    {WIFI_PSK,           {WIFI_PSK, 53, "WiFi Passphrase", "wifi_psk", FURBLE_STR}            },
    {NTP,                {NTP, 54, "NTP", "ntp", FURBLE_STR}                                  },
    {NTP_SERVER,         {NTP_SERVER, 55, "NTP Server", "ntp_server", FURBLE_STR}             },
    {TIMEZONE,           {TIMEZONE, 42, "Time zone", "timezone", FURBLE_STR}                  },
    {IVL_SLEEP,          {IVL_SLEEP, 75, "Deep Sleep", "ivl_sleep", FURBLE_STR}               },
    {IVL_SLEEP_THR,      {IVL_SLEEP_THR, 76, "Sleep Threshold", "ivl_sleep_thr", FURBLE_STR}  },
};

const Settings::setting_t &Settings::get(type_t type) {
  return m_Setting.at(type);
}

const Settings::setting_t *Settings::getByWireId(uint8_t wire_id) {
  if (wire_id == 0) {
    return nullptr;
  }

  for (const auto &it : m_Setting) {
    if (it.second.wire_id == wire_id) {
      return &it.second;
    }
  }

  return nullptr;
}

const std::unordered_map<Settings::type_t, Settings::setting_t> &Settings::all(void) {
  return m_Setting;
}

bool Settings::appliesImmediately(type_t type) {
  switch (type) {
    case GPS:
    case GPS_BAUD:
    case GPS_RATE:
    case GPS_NMEA:
    case GPS_CONSTEL:
    case MULTICONNECT:
    case RECONNECT:
    case RECON_BACKOFF:
    case FAUXNY:
    case AUTOCONNECT:
    case CPU_FREQ:
    case BATT_STYLE:
    case SLEEP_CONN:
    case SCAN_MODE:
    case SCAN_TIMEOUT:
    case TX_ADAPTIVE:
    // TX_POWER takes effect live: the companion and console reload paths call
    // Control::setPower on save, so no restart is required to apply it.
    case TX_POWER:
    case GPS_POWER:
    case GPS_DUTY:
    case GPS_ASSIST:
    case GPS_HOLD:
    case GPS_EXTRAP:
    case GPS_ANA:
    case GPS_PLATFORM:
    case GPS_GNSS:
    case GPS_PSM:
    case GPS_MOTION:
    case IR:
    case IR_PROTO:
    case FB_EVENTS:
    case FB_VOLUME:
    case AUTO_OFF:
    case LOW_BATT:
    case AUTO_OFF_CHARGING:
    case SD_GPX:
    case GPX_PERIOD:
    case IMU_WAKE:
    case IMU_TRIG:
    case WIFI:
    case NTP:
    case NTP_SERVER:
    // The time zone is applied to the C library by the setting save path, so
    // the next local-time view uses it without a restart.
    case TIMEZONE:
    // The intervalometer reads these policies on each delay boundary, so a
    // setting write affects the next shot without requiring a reboot.
    case IVL_SLEEP:
    case IVL_SLEEP_THR:
#if defined(FURBLE_MQTT) && FURBLE_MQTT
    case MQTT:
    case MQTT_URI:
    case MQTT_USER:
    case MQTT_PASS:
    case MQTT_BASE:
    case MQTT_HA:
#endif
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
    case WEB_UI:
#endif
#if !defined(FURBLE_NO_DISPLAY)
    case DISPLAY_MODE:
#endif
      return true;
    case BRIGHTNESS:
    case INACTIVITY:
    case DISPLAY_OFF:
    case THEME:
    case TEXT_SIZE:
    case LEGEND:
    case INTERVAL:
    case TOUCH_CALIBRATION:
    case MULTISELECT:
    case SHOW_TITLE:
    case BULB:
    case COMPANION:
    case COMPANION_PASSWORD:
    case CONN_SAVER:
    case FB_OUTPUT:
    case PRESET_PICKER:
    case BUTTON_MODE:
    // The IMU is brought up during Platform init, so a save takes effect on the
    // next restart rather than immediately. The motion engine is chosen when
    // the source is armed, which is also Platform init, so it follows the IMU.
    case IMU:
    case HW_MOTION:
    // The boot screen is only read at startup, so a save takes effect next boot.
    case BOOT_SPLASH:
    // The profile is applied through the effective accessors, which are read at
    // boot for the display bundle and on the next connect for the link bundle.
    // A reboot guarantees the whole bundle, so report it as not immediate.
    case BATTERY_SAVER:
#if defined(FURBLE_M5STICKS3)
    case WATCHDOG:
#endif
    case WIFI_SSID:
    case WIFI_PSK:
      return false;
  }
  return false;
}

bool Settings::isDangerous(type_t type) {
  switch (type) {
    case TX_POWER:
    case TX_ADAPTIVE:
    case CPU_FREQ:
    case SLEEP_CONN:
    case COMPANION:
    case COMPANION_PASSWORD:
    // Enabling the profile changes connection and sleep behaviour, the same
    // link-affecting class as the SLEEP_CONN it bundles.
    case BATTERY_SAVER:
      return true;
    case BRIGHTNESS:
    case INACTIVITY:
    case DISPLAY_OFF:
    case THEME:
    case TEXT_SIZE:
    case LEGEND:
    case PRESET_PICKER:
    case GPS:
    case GPS_BAUD:
    case GPS_RATE:
    case GPS_NMEA:
    case GPS_CONSTEL:
    case GPS_POWER:
    case GPS_DUTY:
    case GPS_ASSIST:
    case GPS_HOLD:
    case GPS_EXTRAP:
    case GPS_ANA:
    case GPS_PLATFORM:
    case GPS_GNSS:
    case GPS_PSM:
    case INTERVAL:
    case MULTICONNECT:
    case RECONNECT:
    case RECON_BACKOFF:
    case FAUXNY:
    case TOUCH_CALIBRATION:
    case MULTISELECT:
    case AUTOCONNECT:
    case BATT_STYLE:
    case SHOW_TITLE:
    case BULB:
    case SCAN_MODE:
    case SCAN_TIMEOUT:
    case CONN_SAVER:
    case IR:
    case IR_PROTO:
    case FB_OUTPUT:
    case FB_EVENTS:
    case FB_VOLUME:
    case BUTTON_MODE:
    case AUTO_OFF:
    case LOW_BATT:
    case AUTO_OFF_CHARGING:
    case SD_GPX:
    case GPX_PERIOD:
    case IMU:
    case IMU_WAKE:
    case IMU_TRIG:
    case HW_MOTION:
    case GPS_MOTION:
    case IVL_SLEEP:
    case IVL_SLEEP_THR:
    case BOOT_SPLASH:
#if defined(FURBLE_MQTT) && FURBLE_MQTT
    case MQTT:
    case MQTT_URI:
    case MQTT_USER:
    case MQTT_PASS:
    case MQTT_BASE:
    case MQTT_HA:
#endif
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
    case WEB_UI:
#endif
#if !defined(FURBLE_NO_DISPLAY)
    case DISPLAY_MODE:
#endif
#if defined(FURBLE_M5STICKS3)
    case WATCHDOG:
#endif
    case WIFI:
    case WIFI_SSID:
    case WIFI_PSK:
    case NTP:
    case NTP_SERVER:
    case TIMEZONE:
      return false;
  }
  return false;
}

bool Settings::savePassword(const std::string &value) {
  const auto &setting = get(COMPANION_PASSWORD);
  Preferences prefs;
  return prefs.begin(setting.nvs_namespace, false) && prefs.putString(setting.key, value.c_str());
}

bool Settings::loadPassword(std::string &value) {
  const auto &setting = get(COMPANION_PASSWORD);
  Preferences prefs;
  if (!prefs.begin(setting.nvs_namespace, true)) {
    value.clear();
    return false;
  }

  std::string loaded;
  const auto result = prefs.getString(setting.key, loaded);
  prefs.end();
  if (result == Preferences::string_result_t::NOT_FOUND) {
    value.clear();
    return true;
  }
  if (result != Preferences::string_result_t::OK) {
    value.clear();
    return false;
  }
  value = loaded;
  return true;
}

template <typename T>
T Settings::loadValue(type_t type) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, true);
  T value = prefs.get<T>(setting.key, T {});
  prefs.end();
  return value;
}

template <typename T>
void Settings::saveValue(type_t type, const T &value) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, false);
  prefs.put<T>(setting.key, value);
  prefs.end();
}

template <>
bool Settings::load<bool>(type_t type) {
  return loadValue<bool>(type);
}

template <>
uint8_t Settings::load<uint8_t>(type_t type) {
  return loadValue<uint8_t>(type);
}

template <>
uint32_t Settings::load<uint32_t>(type_t type) {
  return loadValue<uint32_t>(type);
}

template <>
uint16_t Settings::load<uint16_t>(type_t type) {
  return loadValue<uint16_t>(type);
}

template <>
std::string Settings::load<std::string>(type_t type) {
  return loadValue<std::string>(type);
}

template <>
interval_t Settings::load<interval_t>(type_t type) {
  const auto &setting = get(type);
  interval_t interval;
  Preferences prefs;

  prefs.begin(setting.nvs_namespace, true);
  size_t len = prefs.get(setting.key, &interval, sizeof(interval_t));
  if (len == sizeof(interval_v1_t)) {
    // migrate v1 interval settings
    interval.wait = INTERVAL_DEFAULT_WAIT;
  } else if (len != sizeof(interval_t)) {
    // default values
    interval.count = INTERVAL_DEFAULT_COUNT;
    interval.delay = INTERVAL_DEFAULT_DELAY;
    interval.shutter = INTERVAL_DEFAULT_SHUTTER;
    interval.wait = INTERVAL_DEFAULT_WAIT;
  }

  prefs.end();

  return interval;
}

template <>
SpinValue::nvs_t Settings::load<SpinValue::nvs_t>(type_t type) {
  const auto &setting = get(type);
  SpinValue::nvs_t nvs;
  Preferences prefs;

  prefs.begin(setting.nvs_namespace, true);
  size_t len = prefs.get(setting.key, &nvs, sizeof(SpinValue::nvs_t));
  if (len != sizeof(SpinValue::nvs_t)) {
    // default value
    nvs = BULB_DEFAULT;
  }

  prefs.end();

  return nvs;
}

template <>
esp_power_level_t Settings::load<esp_power_level_t>(type_t type) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, true);
  uint8_t value = prefs.get<uint8_t>(setting.key);
  prefs.end();

  switch (value) {
    case 0:
      return ESP_PWR_LVL_P3;
    case 1:
      return ESP_PWR_LVL_P6;
    case 2:
      return ESP_PWR_LVL_P9;
    default:
      return ESP_PWR_LVL_P3;
  }
}

template <>
Settings::calibration_t Settings::load<Settings::calibration_t>(type_t type) {
  const auto &setting = get(type);
  calibration_t calibration;
  Preferences prefs;

  prefs.begin(setting.nvs_namespace, true);
  size_t len = prefs.get(setting.key, &calibration, sizeof(calibration_t));
  if (len != sizeof(calibration_t)) {
    // default values
    calibration.points[0] = 0;
    calibration.points[1] = 0;
    calibration.points[2] = 0;
    calibration.points[3] = 0;
    calibration.points[4] = 0;
    calibration.points[5] = 0;
    calibration.points[6] = 0;
    calibration.points[7] = 0;
    calibration.calibrated = false;
  }

  prefs.end();

  return calibration;
}

template <>
void Settings::save<bool>(const type_t type, const bool &value) {
  saveValue<bool>(type, value);
}

template <>
void Settings::save<uint8_t>(const type_t type, const uint8_t &value) {
  saveValue<uint8_t>(type, value);
  if (type == TIMEZONE) {
    // Every ingress path (UI, console, companion, provisioning, SD import)
    // lands here, so the process time zone follows the stored value.
    (void)Timezone::apply(Timezone::clamp(value));
  }
}

template <>
void Settings::save<uint32_t>(const type_t type, const uint32_t &value) {
  saveValue<uint32_t>(type, value);
}

template <>
void Settings::save<uint16_t>(const type_t type, const uint16_t &value) {
  saveValue<uint16_t>(type, value);
}

template <>
void Settings::save<interval_t>(const type_t type, const interval_t &value) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, false);
  prefs.put(setting.key, &value, sizeof(value));
  prefs.end();
}

template <>
Settings::multiselect_t Settings::load<Settings::multiselect_t>(type_t type) {
  const auto &setting = get(type);
  multiselect_t selection = {};
  Preferences prefs;

  prefs.begin(setting.nvs_namespace, true);
  size_t len = prefs.get(setting.key, &selection, sizeof(selection));
  if (len != sizeof(selection)) {
    // A record written before MULTISELECT_NAME_MAX widened is still valid
    // data. Read it back in its own layout and widen it, so a remembered set
    // survives the upgrade instead of being silently dropped.
    selection = {};
    multiselect_legacy_t legacy = {};
    if (prefs.get(setting.key, &legacy, sizeof(legacy)) == sizeof(legacy)) {
      selection = multiselectFromLegacy(legacy);
    }
  }
  prefs.end();

  return selection;
}

template <>
void Settings::save<SpinValue::nvs_t>(const type_t type, const SpinValue::nvs_t &value) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, false);
  prefs.put(setting.key, &value, sizeof(value));
  prefs.end();
}

template <>
void Settings::save<std::string>(const type_t type, const std::string &value) {
  saveValue<std::string>(type, value);
}

template <>
void Settings::save<Settings::calibration_t>(const type_t type, const calibration_t &value) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, false);
  prefs.put(setting.key, &value, sizeof(value));
  prefs.end();
}

template <>
void Settings::save<Settings::multiselect_t>(const type_t type, const multiselect_t &value) {
  const auto &setting = get(type);
  Preferences prefs;
  prefs.begin(setting.nvs_namespace, false);
  prefs.put(setting.key, &value, sizeof(value));
  prefs.end();
}

namespace {

constexpr const char *LOG_TAG = "settings";
constexpr const char *DEFAULTS_VERSION_KEY = "cfg_def_ver";
constexpr size_t MAX_MISSING_DEFAULTS = 128;

template <typename T>
bool putDefault(Preferences &prefs, const char *key, const T &value) {
  return prefs.put<T>(key, value) != 0;
}

bool putDefault(Preferences &prefs, const char *key, const char *value) {
  return prefs.putString(key, value);
}

template <typename T>
bool putDefaultBlob(Preferences &prefs, const char *key, const T &value) {
  return prefs.put(key, &value, sizeof(value)) == sizeof(value);
}

/**
 * Write the first-boot value for one setting into an already open writer.
 *
 * The caller has enabled deferred commits, so this stages every key in one NVS
 * transaction per namespace. The values and the board conditional choices are
 * the same as the per-setting seeds this replaced.
 */
bool applyDefault(Preferences &prefs, Settings::type_t type) {
  const auto &setting = Settings::get(type);
  switch (type) {
    case Settings::BRIGHTNESS:
      return putDefault<uint8_t>(prefs, setting.key, 128);
    case Settings::INACTIVITY:
      // 60 secs. A fresh device used to ship with Never, which left the panel
      // and its backlight on until the user acted.
      return putDefault<uint8_t>(prefs, setting.key, SettingsDefaults::INACTIVITY);
    case Settings::DISPLAY_OFF:
      // Off. The old default was Dim, which still drove the panel.
      return putDefault<uint8_t>(prefs, setting.key, SettingsDefaults::displayOff());
    case Settings::THEME:
      return putDefault(prefs, setting.key, "Default");
    case Settings::TEXT_SIZE:
      // The default is board conditional. Large screens keep Normal, but the
      // 80x160 M5StickC starts a fresh device at Small because Normal is
      // already dense on that tiny panel and Large does not fit. This only
      // seeds an unset key, so an existing device keeps its stored choice.
      return putDefault<uint8_t>(prefs, setting.key, TextSizePolicy::DEFAULT);
    case Settings::BUTTON_MODE:
      return putDefault(prefs, setting.key, Settings::BUTTON_MODE_TWO_BUTTON_VALUE);
    case Settings::COMPANION_PASSWORD:
      // Missing passwords already mean unset. Never turn a failed existence
      // check into a write that clears an existing authentication gate.
      return true;
    case Settings::TX_POWER:
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::GPS_PLATFORM:
    // 0 follows GPS_CONSTEL, and power save is opt-in.
    case Settings::GPS_GNSS:
    case Settings::GPS_PSM:
    case Settings::IR_PROTO:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
    case Settings::GPS_HOLD:
    case Settings::IMU_WAKE:
      return putDefault<uint8_t>(prefs, setting.key, 0);
    case Settings::AUTO_OFF_CHARGING:
      return putDefault<bool>(prefs, setting.key, false);
    // AssistNow Autonomous is off in the M10 ROM but on in furble: it costs
    // nothing until the receiver has orbits to predict and turns a cold start
    // with a few days' old data into a 3 to 4 s fix (plan 321).
    case Settings::GPS_ANA:
      return putDefault<bool>(prefs, setting.key, true);
    case Settings::BATT_STYLE:
      return putDefault<uint8_t>(prefs, setting.key, Settings::BATT_STYLE_ICON);
    case Settings::LEGEND:
      // The legend beside the button it names is what these boards shipped
      // before the setting existed, so it stays the default.
      return putDefault<uint8_t>(prefs, setting.key, Settings::LEGEND_BUTTONS);
    case Settings::SHOW_TITLE:
      return putDefault<bool>(prefs, setting.key, true);
    case Settings::INTERVAL:
    {
      const interval_t interval = {
          INTERVAL_DEFAULT_COUNT,
          INTERVAL_DEFAULT_DELAY,
          INTERVAL_DEFAULT_SHUTTER,
          INTERVAL_DEFAULT_WAIT,
      };
      return putDefaultBlob(prefs, setting.key, interval);
    }
    case Settings::BULB:
      return putDefaultBlob(prefs, setting.key, Settings::BULB_DEFAULT);
    // The user asked for the boot screen, so ship it on by default. The toggle
    // restores the old no-splash boot.
    case Settings::BOOT_SPLASH:
      return putDefault<bool>(prefs, setting.key, true);
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
      return putDefault<bool>(prefs, setting.key, true);
#endif
    case Settings::MULTISELECT:
    {
      const Settings::multiselect_t selection = {};
      return putDefaultBlob(prefs, setting.key, selection);
    }
    case Settings::IVL_SLEEP_THR:
      return putDefault<uint32_t>(prefs, setting.key, Settings::IVL_SLEEP_THR_DEFAULT);
    case Settings::GPS:
    case Settings::IMU:
    case Settings::IMU_TRIG:
    case Settings::GPS_NMEA:
    case Settings::GPS_EXTRAP:
    case Settings::GPS_MOTION:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::TX_ADAPTIVE:
    case Settings::CONN_SAVER:
    case Settings::IR:
    case Settings::PRESET_PICKER:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::COMPANION:
    case Settings::SD_GPX:
    // Default off keeps today's behaviour, the profile is strictly opt-in.
    case Settings::BATTERY_SAVER:
    case Settings::IVL_SLEEP:
      return putDefault<bool>(prefs, setting.key, false);
    case Settings::RECON_BACKOFF:
      // Default on. A camera that is off or out of range otherwise retries a
      // scan every 5 s forever; the 2.5 s first retry still recovers a stale
      // session quickly, and later attempts back off to 120 s.
      return putDefault<bool>(prefs, setting.key, true);
    case Settings::SLEEP_CONN:
      // Only the StickS3 has the modem-sleep controller and the GPS burst
      // lock. Fresh devices ship with the low-power connected state on;
      // existing devices keep their stored choice.
      return putDefault<bool>(prefs, setting.key, SettingsDefaults::sleepConn());
    case Settings::HW_MOTION:
      // Ships as Software, not Auto. Auto prefers the board's hardware engine,
      // and that path is unproven on hardware, so the default must not select
      // it. Auto becomes the default in a follow-up commit once the six-step
      // gate in plans/implemented/20-imu-hw-motion.md passes on the S3.
      return putDefault<uint8_t>(prefs, setting.key, Settings::HW_MOTION_SOFTWARE);
    case Settings::GPS_BAUD:
      return putDefault<uint32_t>(prefs, setting.key, Settings::BAUD_9600);
    case Settings::SCAN_TIMEOUT:
      // A bounded first boot instead of Never. A forgotten Scan page used to
      // scan at full receive duty until the user left it. An explicit Never is
      // still stored as zero and is not rewritten.
      return putDefault<uint32_t>(prefs, setting.key, Settings::SCAN_TIMEOUT_DEFAULT);
    case Settings::GPX_PERIOD:
      return putDefault<uint16_t>(prefs, setting.key, Settings::GPX_PERIOD_DEFAULT);
    case Settings::CPU_FREQ:
      return putDefault<uint8_t>(prefs, setting.key, Settings::CPU_FREQ_DEFAULT);
    case Settings::FB_OUTPUT:
      return putDefault<uint8_t>(prefs, setting.key, 0);
    case Settings::FB_EVENTS:
      return putDefault<uint8_t>(prefs, setting.key, 0x0F);
    case Settings::FB_VOLUME:
      return putDefault<uint8_t>(prefs, setting.key, 64);
#if defined(FURBLE_MQTT) && FURBLE_MQTT
    case Settings::MQTT:
    case Settings::MQTT_HA:
      return putDefault<bool>(prefs, setting.key, false);
    case Settings::MQTT_URI:
    case Settings::MQTT_USER:
    case Settings::MQTT_PASS:
      return putDefault(prefs, setting.key, "");
    case Settings::MQTT_BASE:
      return putDefault(prefs, setting.key, "furble");
#endif
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
    case Settings::WEB_UI:
      return putDefault<bool>(prefs, setting.key, false);
#endif
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
      return putDefault<uint8_t>(prefs, setting.key, static_cast<uint8_t>(Settings::GUI));
#endif
    case Settings::WIFI:
    case Settings::NTP:
      return putDefault<bool>(prefs, setting.key, false);
    case Settings::WIFI_SSID:
    case Settings::WIFI_PSK:
      return putDefault(prefs, setting.key, "");
    case Settings::NTP_SERVER:
      return putDefault(prefs, setting.key, "pool.ntp.org");
    case Settings::TIMEZONE:
      // UTC preserves the previous camera clock behavior. A user who wants
      // local civil time selects a zone; nothing changes silently.
      return putDefault<uint8_t>(prefs, setting.key, 0);
    case Settings::TOUCH_CALIBRATION:
    {
      const Settings::calibration_t calibration = {
          .points = {0x00},
          .calibrated = false,
      };
      return putDefaultBlob(prefs, setting.key, calibration);
    }
  }
  return true;
}

size_t namespaceIndex(const char *const *namespaces, size_t count, const char *name) {
  for (size_t index = 0; index < count; index++) {
    if (std::strcmp(namespaces[index], name) == 0) {
      return index;
    }
  }
  return count;
}

}  // namespace

void Settings::init(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // A matching marker means every default was committed by an earlier boot.
  // Skip the per-key existence scan, which is the common case after the first
  // boot with this firmware.
  bool scan = true;
  {
    Preferences marker;
    if (marker.begin(FURBLE_STR, true)) {
      // A missing or wrong-typed key reads as zero, which always forces the
      // scan. Avoid isKey() here: it probes every integer type before the
      // marker lookup, and this runs on every boot.
      const uint32_t stored = marker.get<uint32_t>(DEFAULTS_VERSION_KEY, 0);
      scan = SettingsBootstrap::needsDefaultScan(stored, SettingsBootstrap::DEFAULTS_VERSION);
      marker.end();
    }
  }
  if (!scan) {
    return;
  }

  // Find every namespace once. The table has two today; the bound keeps the
  // batch allocation-free and a future third namespace still fits.
  const char *namespaces[SettingsBootstrap::MAX_BATCH_NAMESPACES] = {};
  size_t namespaceCount = 0;
  bool namespaceOverflow = false;
  for (const auto &it : m_Setting) {
    const char *name = it.second.nvs_namespace;
    if (namespaceIndex(namespaces, namespaceCount, name) != namespaceCount) {
      continue;
    }
    if (namespaceCount == SettingsBootstrap::MAX_BATCH_NAMESPACES) {
      namespaceOverflow = true;
      break;
    }
    namespaces[namespaceCount++] = name;
  }

  const setting_t *missing[MAX_MISSING_DEFAULTS] = {};
  size_t missingCount = 0;
  bool missingOverflow = false;
  auto recordMissing = [&missing, &missingCount, &missingOverflow](const setting_t &setting) {
    if (missingCount < MAX_MISSING_DEFAULTS) {
      missing[missingCount++] = &setting;
    } else {
      missingOverflow = true;
    }
  };

  if (!namespaceOverflow) {
    Preferences readers[SettingsBootstrap::MAX_BATCH_NAMESPACES];
    bool opened[SettingsBootstrap::MAX_BATCH_NAMESPACES] = {};
    for (size_t index = 0; index < namespaceCount; index++) {
      opened[index] = readers[index].begin(namespaces[index], true);
    }

    for (const auto &it : m_Setting) {
      const setting_t &setting = it.second;
      if (setting.type == COMPANION_PASSWORD) {
        continue;
      }
      const size_t index = namespaceIndex(namespaces, namespaceCount, setting.nvs_namespace);
      if ((index >= namespaceCount) || !opened[index] || !readers[index].isKey(setting.key)) {
        recordMissing(setting);
      }
    }
    for (size_t index = 0; index < namespaceCount; index++) {
      readers[index].end();
    }
  } else {
    // The table outgrew the fixed namespace bound. Keep the old per-key check
    // rather than dropping a default; the write path below also falls back to
    // per-key transactions.
    for (const auto &it : m_Setting) {
      const setting_t &setting = it.second;
      if (setting.type == COMPANION_PASSWORD) {
        continue;
      }
      Preferences reader;
      if (!reader.begin(setting.nvs_namespace, true) || !reader.isKey(setting.key)) {
        recordMissing(setting);
      }
      reader.end();
    }
  }

  bool writesOk = !missingOverflow;
  bool seededTimezone = false;
  if (namespaceOverflow) {
    // Per-key fallback, used only if a future settings table exceeds the
    // namespace bound. Each put keeps its own commit, exactly like the old
    // implementation.
    for (size_t index = 0; index < missingCount; index++) {
      const setting_t &setting = *missing[index];
      Preferences writer;
      if (!writer.begin(setting.nvs_namespace, false)) {
        writesOk = false;
        continue;
      }
      if (setting.type == TIMEZONE) {
        seededTimezone = true;
      }
      if (!applyDefault(writer, setting.type)) {
        writesOk = false;
      }
      writer.end();
    }
  } else if (missingCount > 0) {
    Preferences writers[SettingsBootstrap::MAX_BATCH_NAMESPACES];
    bool writerOpen[SettingsBootstrap::MAX_BATCH_NAMESPACES] = {};

    for (size_t index = 0; index < missingCount; index++) {
      const setting_t &setting = *missing[index];
      const size_t ns = namespaceIndex(namespaces, namespaceCount, setting.nvs_namespace);
      if (ns >= namespaceCount) {
        writesOk = false;
        continue;
      }
      if (!writerOpen[ns]) {
        if (!writers[ns].begin(namespaces[ns], false)) {
          writesOk = false;
          continue;
        }
        writers[ns].setDeferredCommit(true);
        writerOpen[ns] = true;
      }
      if (setting.type == TIMEZONE) {
        seededTimezone = true;
      }
      if (!applyDefault(writers[ns], setting.type)) {
        writesOk = false;
      }
    }

    for (size_t index = 0; index < namespaceCount; index++) {
      if (!writerOpen[index]) {
        continue;
      }
      if (!writers[index].commit()) {
        writesOk = false;
      }
      writers[index].end();
    }
  }

  if (writesOk && seededTimezone) {
    (void)Timezone::apply(Timezone::clamp(0));
  }
  if (!writesOk) {
    // Do not write the marker. The next boot rescans and retries the seed.
    ESP_LOGE(LOG_TAG, "settings default seed failed, will retry on the next boot");
    return;
  }

  Preferences marker;
  if (!marker.begin(FURBLE_STR, false)
      || (marker.put<uint32_t>(DEFAULTS_VERSION_KEY, SettingsBootstrap::DEFAULTS_VERSION)
          != sizeof(uint32_t))) {
    ESP_LOGE(LOG_TAG, "settings default marker write failed");
  }
  marker.end();

  if (missingCount > 0) {
    ESP_LOGI(LOG_TAG, "settings default seed: %u settings in %u namespaces",
             static_cast<unsigned>(missingCount), static_cast<unsigned>(namespaceCount));
  }
}

bool Settings::batterySaver(void) {
  return load<BATTERY_SAVER>();
}

bool Settings::multiselectAdd(multiselect_t &selection, const char *name) {
  if ((name == nullptr) || (selection.count >= MULTISELECT_MAX)) {
    return false;
  }
  // A name that does not fit would be stored truncated, and a truncated entry
  // compares equal to a different camera whose whole name is that prefix. Refuse
  // it, so the set forgets one body rather than ticking another one.
  if (strlen(name) >= MULTISELECT_NAME_MAX) {
    return false;
  }

  snprintf(selection.name[selection.count], MULTISELECT_NAME_MAX, "%s", name);
  selection.count++;
  return true;
}

bool Settings::multiselectContains(const multiselect_t &selection, const char *name) {
  if (name == nullptr) {
    return false;
  }

  const size_t count = std::min<size_t>(selection.count, MULTISELECT_MAX);
  for (size_t i = 0; i < count; i++) {
    // A stored slot from a corrupt record need not be terminated, so bound the
    // read before comparing it as a string.
    if (strnlen(selection.name[i], MULTISELECT_NAME_MAX) >= MULTISELECT_NAME_MAX) {
      continue;
    }
    if (strcmp(selection.name[i], name) == 0) {
      return true;
    }
  }
  return false;
}

Settings::multiselect_t Settings::multiselectFromLegacy(const multiselect_legacy_t &legacy) {
  multiselect_t selection = {};

  selection.count = static_cast<uint8_t>(std::min<size_t>(legacy.count, MULTISELECT_MAX));
  for (size_t i = 0; i < MULTISELECT_MAX; i++) {
    memcpy(selection.name[i], legacy.name[i], MULTISELECT_NAME_LEGACY_MAX);
    selection.name[i][MULTISELECT_NAME_LEGACY_MAX - 1] = '\0';
  }
  return selection;
}

bool Settings::sleepConnEffective(void) {
  const bool stored = load<SLEEP_CONN>();
#if defined(FURBLE_M5STICKS3)
  // Only the StickS3 has the modem-sleep controller and the GPS burst lock, so
  // it is the only board where forcing sleep-while-connected is both effective
  // and safe. See plans/98 and the FurbleBatterySaver header note.
  return BatterySaver::sleepConn(batterySaver(), stored, true);
#else
  return BatterySaver::sleepConn(batterySaver(), stored, false);
#endif
}

bool Settings::connSaverEffective(void) {
  return BatterySaver::connSaver(batterySaver(), load<CONN_SAVER>());
}

bool Settings::reconBackoffEffective(void) {
  return BatterySaver::reconBackoff(batterySaver(), load<RECON_BACKOFF>());
}

uint8_t Settings::scanModeEffective(void) {
  return BatterySaver::scanMode(batterySaver(), load<SCAN_MODE>());
}

uint8_t Settings::inactivityEffective(void) {
  return BatterySaver::inactivity(batterySaver(), load<INACTIVITY>());
}

uint8_t Settings::displayOffEffective(void) {
  return BatterySaver::displayOff(batterySaver(), load<DISPLAY_OFF>());
}
}  // namespace Furble
