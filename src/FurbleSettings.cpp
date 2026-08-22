#include <esp_bt.h>
#include <nvs_flash.h>

#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "Preferences.h"

namespace Furble {
Preferences Settings::m_Prefs;

const std::unordered_map<Settings::type_t, Settings::setting_t> Settings::m_Setting = {
    {BRIGHTNESS,        {BRIGHTNESS, 1, "Brightness", "brightness", "M5ez"}                  },
    {INACTIVITY,        {INACTIVITY, 2, "Inactivity", "inactivity", "M5ez"}                  },
    {DISPLAY_OFF,       {DISPLAY_OFF, 24, "Screen off", "display_off", FURBLE_STR}           },
    {THEME,             {THEME, 3, "Theme", "theme", "M5ez"}                                 },
    {TEXT_SIZE,         {TEXT_SIZE, 40, "Text size", "text_size", FURBLE_STR}                },
    {TX_POWER,          {TX_POWER, 4, "TX Power", "tx_power", FURBLE_STR}                    },
    {TX_ADAPTIVE,       {TX_ADAPTIVE, 28, "Adaptive", "tx_adaptive", FURBLE_STR}             },
    {GPS,               {GPS, 5, "GPS", "gps", FURBLE_STR}                                   },
    {GPS_BAUD,          {GPS_BAUD, 6, "GPS Baud", "gps_baud", FURBLE_STR}                    },
    {GPS_RATE,          {GPS_RATE, 13, "GPS Rate", "gps_rate", FURBLE_STR}                   },
    {GPS_NMEA,          {GPS_NMEA, 14, "GPS Sentences", "gps_nmea", FURBLE_STR}              },
    {GPS_CONSTEL,       {GPS_CONSTEL, 15, "GPS Constellation", "gps_constel", FURBLE_STR}    },
    {GPS_POWER,         {GPS_POWER, 25, "GPS Power", "gps_power", FURBLE_STR}                },
    {GPS_DUTY,          {GPS_DUTY, 26, "GPS Duty", "gps_duty", FURBLE_STR}                   },
    {GPS_ASSIST,        {GPS_ASSIST, 41, "GPS Assistance", "gps_assist", FURBLE_STR}         },
    {INTERVAL,          {INTERVAL, 7, "Interval", "interval", FURBLE_STR}                    },
    {MULTICONNECT,      {MULTICONNECT, 8, "Multi-Connect", "multiconnect", FURBLE_STR}       },
    {RECONNECT,         {RECONNECT, 9, "Infinite-ReConnect", "reconnect", FURBLE_STR}        },
    {RECON_BACKOFF,     {RECON_BACKOFF, 16, "Reconnect Backoff", "recon_backoff", FURBLE_STR}},
    {FAUXNY,            {FAUXNY, 10, "FauxNY", "fauxNY", FURBLE_STR}                         },
    {TOUCH_CALIBRATION, {TOUCH_CALIBRATION, 0, "Touch Calibration", "t_calib", FURBLE_STR}   },
    {AUTOCONNECT,       {AUTOCONNECT, 11, "Auto-Connect", "autoconnect", FURBLE_STR}         },
    {COMPANION,         {COMPANION, 12, "Companion", "companion", FURBLE_STR}                },
    {CPU_FREQ,          {CPU_FREQ, 17, "CPU Speed", "cpu_freq", FURBLE_STR}                  },
    {BATT_STYLE,        {BATT_STYLE, 18, "Battery Style", "batt_style", FURBLE_STR}          },
    {SHOW_TITLE,        {SHOW_TITLE, 19, "Show Title", "show_title", FURBLE_STR}             },
    {SLEEP_CONN,        {SLEEP_CONN, 20, "Sleep while connected", "sleep_conn", FURBLE_STR}  },
    {BULB,              {BULB, 0, "Bulb", "bulb", FURBLE_STR}                                },
    {SCAN_MODE,         {SCAN_MODE, 21, "Scan Mode", "scan_mode", FURBLE_STR}                },
    {SCAN_TIMEOUT,      {SCAN_TIMEOUT, 22, "Scan Timeout", "scan_timeout", FURBLE_STR}       },
    {CONN_SAVER,        {CONN_SAVER, 29, "Connection power save", "conn_saver", FURBLE_STR}  },
    {IR,                {IR, 31, "Infrared", "ir", FURBLE_STR}                               },
    {IR_PROTO,          {IR_PROTO, 32, "IR Protocol", "ir_proto", FURBLE_STR}                },
    {FB_OUTPUT,         {FB_OUTPUT, 33, "Feedback", "fb_output", FURBLE_STR}                 },
    {FB_EVENTS,         {FB_EVENTS, 34, "Feedback Events", "fb_events", FURBLE_STR}          },
    {FB_VOLUME,         {FB_VOLUME, 35, "Volume", "fb_volume", FURBLE_STR}                   },
    {PRESET_PICKER,     {PRESET_PICKER, 30, "Preset Picker", "preset_picker", FURBLE_STR}    },
    {BUTTON_MODE,       {BUTTON_MODE, 27, "Button Mode", "button_mode", FURBLE_STR}          },
    {AUTO_OFF,          {AUTO_OFF, 37, "Auto off", "auto_off", FURBLE_STR}                   },
    {LOW_BATT,          {LOW_BATT, 38, "Low battery", "low_batt", FURBLE_STR}                },
    {SD_GPX,            {SD_GPX, 39, "GPX Logging", "sd_gpx", FURBLE_STR}                    },
    {GPX_PERIOD,        {GPX_PERIOD, 0, "GPX Interval", "gpx_period", FURBLE_STR}            },
    {BOOT_SPLASH,       {BOOT_SPLASH, 44, "Boot screen", "boot_splash", FURBLE_STR}          },
#if !defined(FURBLE_NO_DISPLAY)
    {DISPLAY_MODE,      {DISPLAY_MODE, 36, "Display Mode", "display_mode", FURBLE_STR}       },
#endif
#if defined(FURBLE_M5STICKS3)
    {WATCHDOG,          {WATCHDOG, 23, "Watchdog", "watchdog", FURBLE_STR}                   },
#endif
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
    case IR:
    case IR_PROTO:
    case FB_EVENTS:
    case FB_VOLUME:
    case AUTO_OFF:
    case LOW_BATT:
    case SD_GPX:
    case GPX_PERIOD:
#if !defined(FURBLE_NO_DISPLAY)
    case DISPLAY_MODE:
#endif
      return true;
    case BRIGHTNESS:
    case INACTIVITY:
    case DISPLAY_OFF:
    case THEME:
    case TEXT_SIZE:
    case INTERVAL:
    case TOUCH_CALIBRATION:
    case SHOW_TITLE:
    case BULB:
    case COMPANION:
    case CONN_SAVER:
    case FB_OUTPUT:
    case PRESET_PICKER:
    case BUTTON_MODE:
    // The boot screen is only read at startup, so a save takes effect next boot.
    case BOOT_SPLASH:
#if defined(FURBLE_M5STICKS3)
    case WATCHDOG:
#endif
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
      return true;
    case BRIGHTNESS:
    case INACTIVITY:
    case DISPLAY_OFF:
    case THEME:
    case TEXT_SIZE:
    case PRESET_PICKER:
    case GPS:
    case GPS_BAUD:
    case GPS_RATE:
    case GPS_NMEA:
    case GPS_CONSTEL:
    case GPS_POWER:
    case GPS_DUTY:
    case GPS_ASSIST:
    case INTERVAL:
    case MULTICONNECT:
    case RECONNECT:
    case RECON_BACKOFF:
    case FAUXNY:
    case TOUCH_CALIBRATION:
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
    case SD_GPX:
    case GPX_PERIOD:
    case BOOT_SPLASH:
#if !defined(FURBLE_NO_DISPLAY)
    case DISPLAY_MODE:
#endif
#if defined(FURBLE_M5STICKS3)
    case WATCHDOG:
#endif
      return false;
  }
  return false;
}

template <typename T>
T Settings::loadValue(type_t type) {
  const auto &setting = get(type);
  m_Prefs.begin(setting.nvs_namespace, true);
  T value = m_Prefs.get<T>(setting.key, T {});
  m_Prefs.end();
  return value;
}

template <typename T>
void Settings::saveValue(type_t type, const T &value) {
  const auto &setting = get(type);
  m_Prefs.begin(setting.nvs_namespace, false);
  m_Prefs.put<T>(setting.key, value);
  m_Prefs.end();
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

  m_Prefs.begin(setting.nvs_namespace, true);
  size_t len = m_Prefs.get(setting.key, &interval, sizeof(interval_t));
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

  m_Prefs.end();

  return interval;
}

template <>
SpinValue::nvs_t Settings::load<SpinValue::nvs_t>(type_t type) {
  const auto &setting = get(type);
  SpinValue::nvs_t nvs;

  m_Prefs.begin(setting.nvs_namespace, true);
  size_t len = m_Prefs.get(setting.key, &nvs, sizeof(SpinValue::nvs_t));
  if (len != sizeof(SpinValue::nvs_t)) {
    // default value
    nvs = BULB_DEFAULT;
  }

  m_Prefs.end();

  return nvs;
}

template <>
esp_power_level_t Settings::load<esp_power_level_t>(type_t type) {
  const auto &setting = get(type);
  m_Prefs.begin(setting.nvs_namespace, true);
  uint8_t value = m_Prefs.get<uint8_t>(setting.key);
  m_Prefs.end();

  switch (value) {
    case 0:
      return ESP_PWR_LVL_P3;
    case 1:
      return ESP_PWR_LVL_P6;
    case 2:
      return ESP_PWR_LVL_P9;
  }
  return ESP_PWR_LVL_P3;
}

template <>
Settings::calibration_t Settings::load<Settings::calibration_t>(type_t type) {
  const auto &setting = get(type);
  calibration_t calibration;

  m_Prefs.begin(setting.nvs_namespace, true);
  size_t len = m_Prefs.get(setting.key, &calibration, sizeof(calibration_t));
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

  m_Prefs.end();

  return calibration;
}

template <>
void Settings::save<bool>(const type_t type, const bool &value) {
  saveValue<bool>(type, value);
}

template <>
void Settings::save<uint8_t>(const type_t type, const uint8_t &value) {
  saveValue<uint8_t>(type, value);
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
  m_Prefs.begin(setting.nvs_namespace, false);
  m_Prefs.put(setting.key, &value, sizeof(value));
  m_Prefs.end();
}

template <>
void Settings::save<SpinValue::nvs_t>(const type_t type, const SpinValue::nvs_t &value) {
  const auto &setting = get(type);
  m_Prefs.begin(setting.nvs_namespace, false);
  m_Prefs.put(setting.key, &value, sizeof(value));
  m_Prefs.end();
}

template <>
void Settings::save<std::string>(const type_t type, const std::string &value) {
  saveValue<std::string>(type, value);
}

template <>
void Settings::save<Settings::calibration_t>(const type_t type, const calibration_t &value) {
  const auto &setting = get(type);
  m_Prefs.begin(setting.nvs_namespace, false);
  m_Prefs.put(setting.key, &value, sizeof(value));
  m_Prefs.end();
}

void Settings::init(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Set default values for all settings
  for (const auto &it : m_Setting) {
    auto &setting = it.second;
    m_Prefs.begin(setting.nvs_namespace, true);
    bool exists = m_Prefs.isKey(setting.key);
    m_Prefs.end();

    if (!exists) {
      switch (setting.type) {
        case BRIGHTNESS:
          save<uint8_t>(setting.type, 128);
          break;
        case INACTIVITY:
        case DISPLAY_OFF:
          save<uint8_t>(setting.type, 0);
          break;
        case THEME:
          save<std::string>(setting.type, "Default");
          break;
        case TEXT_SIZE:
          save<uint8_t>(setting.type, TEXT_SIZE_NORMAL);
          break;
        case BUTTON_MODE:
          save<std::string>(setting.type, BUTTON_MODE_TWO_BUTTON_VALUE);
          break;
        case TX_POWER:
        case SCAN_MODE:
        case GPS_RATE:
        case GPS_CONSTEL:
        case GPS_POWER:
        case GPS_DUTY:
        case GPS_ASSIST:
        case IR_PROTO:
        case AUTO_OFF:
        case LOW_BATT:
          save<uint8_t>(setting.type, 0);
          break;
        case BATT_STYLE:
          save<uint8_t>(setting.type, BATT_STYLE_ICON);
          break;
        case SHOW_TITLE:
          save<bool>(setting.type, true);
          break;
        case INTERVAL:
        {
          interval_t interval = {
              INTERVAL_DEFAULT_COUNT,
              INTERVAL_DEFAULT_DELAY,
              INTERVAL_DEFAULT_SHUTTER,
              INTERVAL_DEFAULT_WAIT,
          };
          save<interval_t>(setting.type, interval);
        } break;
        case BULB:
          save<SpinValue::nvs_t>(setting.type, BULB_DEFAULT);
          break;
        // The user asked for the boot screen, so ship it on by default. The
        // toggle restores the old no-splash boot.
        case BOOT_SPLASH:
          save<bool>(setting.type, true);
          break;
#if defined(FURBLE_M5STICKS3)
        case WATCHDOG:
          save<bool>(setting.type, true);
          break;
#endif
        case GPS:
        case GPS_NMEA:
        case MULTICONNECT:
        case RECONNECT:
        case TX_ADAPTIVE:
        case CONN_SAVER:
        case IR:
        case PRESET_PICKER:
        case RECON_BACKOFF:
        case FAUXNY:
        case AUTOCONNECT:
        case SLEEP_CONN:
        case COMPANION:
        case SD_GPX:
          save<bool>(setting.type, false);
          break;
        case GPS_BAUD:
          save<uint32_t>(setting.type, BAUD_9600);
          break;
        case SCAN_TIMEOUT:
          save<uint32_t>(setting.type, 0);
          break;
        case GPX_PERIOD:
          save<uint16_t>(setting.type, GPX_PERIOD_DEFAULT);
          break;
        case CPU_FREQ:
          save<uint8_t>(setting.type, CPU_FREQ_DEFAULT);
          break;
        case FB_OUTPUT:
          save<uint8_t>(setting.type, 0);
          break;
        case FB_EVENTS:
          save<uint8_t>(setting.type, 0x0F);
          break;
        case FB_VOLUME:
          save<uint8_t>(setting.type, 64);
          break;
#if !defined(FURBLE_NO_DISPLAY)
        case DISPLAY_MODE:
          save<uint8_t>(setting.type, static_cast<uint8_t>(GUI));
          break;
#endif
        case TOUCH_CALIBRATION:
        {
          calibration_t calibration = {
              .points = {0x00},
              .calibrated = false,
          };
          save<calibration_t>(setting.type, calibration);
        } break;
      }
    }
  }
}
}  // namespace Furble
