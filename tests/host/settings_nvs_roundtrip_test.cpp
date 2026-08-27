#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "FurbleSettings.h"
#include "Preferences.h"
#include "nvs.h"

// The SD settings helpers have internal linkage because they are only part of
// the SD service's import/export implementation. Including the production
// translation unit here keeps this test on the exact firmware code path while
// the test-only hardware shims make the unrelated SD task code linkable.
#include "../../src/FurbleSD.cpp"

namespace Furble {

// The helper test does not exercise the GPX writer. These definitions satisfy
// the SD service's unrelated task paths without replacing any settings code.
GPX &GPX::getInstance(void) {
  static GPX instance;
  return instance;
}

bool GPX::addPoint(const point_t &, uint16_t) {
  return false;
}

void GPX::close(void) {}

bool GPX::isOpen(void) const {
  return false;
}

}  // namespace Furble

namespace {

using Furble::Settings;

using SettingValue = std::variant<bool,
                                  uint8_t,
                                  uint16_t,
                                  uint32_t,
                                  std::string,
                                  Furble::interval_t,
                                  Furble::SpinValue::nvs_t,
                                  Settings::calibration_t,
                                  Settings::multiselect_t>;

enum class StorageKind {
  BOOL,
  U8,
  U16,
  U32,
  STRING,
  BLOB,
};

struct SettingCase {
  Settings::type_t type;
  const char *name;
  SettingValue default_value;
  SettingValue representative_value;
  StorageKind storage;
};

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

Furble::interval_t defaultInterval() {
  return {
      Furble::INTERVAL_DEFAULT_COUNT,
      Furble::INTERVAL_DEFAULT_DELAY,
      Furble::INTERVAL_DEFAULT_SHUTTER,
      Furble::INTERVAL_DEFAULT_WAIT,
  };
}

Furble::interval_t representativeInterval() {
  return {
      {99, Furble::SpinValue::UNIT_NIL},
      {88, Furble::SpinValue::UNIT_SEC},
      {77, Furble::SpinValue::UNIT_MS },
      {66, Furble::SpinValue::UNIT_MIN},
  };
}

Furble::Settings::calibration_t defaultCalibration() {
  Furble::Settings::calibration_t value = {};
  return value;
}

Furble::Settings::calibration_t representativeCalibration() {
  Furble::Settings::calibration_t value = {};
  for (size_t i = 0; i < 8; i++) {
    value.points[i] = static_cast<uint16_t>(10 + i * 7);
  }
  value.calibrated = true;
  return value;
}

Furble::Settings::multiselect_t defaultMultiselect() {
  Furble::Settings::multiselect_t value = {};
  return value;
}

Furble::Settings::multiselect_t representativeMultiselect() {
  Furble::Settings::multiselect_t value = {};
  std::strncpy(value.name[0], "Landscape", Furble::Settings::MULTISELECT_NAME_MAX - 1);
  std::strncpy(value.name[1], "Portrait", Furble::Settings::MULTISELECT_NAME_MAX - 1);
  value.count = 2;
  return value;
}

std::vector<SettingCase> settingCases() {
  using namespace Furble;
  return {
      {Settings::BRIGHTNESS,        "BRIGHTNESS",        uint8_t {128},                                        uint8_t {77},                StorageKind::U8    },
      {Settings::INACTIVITY,        "INACTIVITY",        uint8_t {0},                                          uint8_t {9},                 StorageKind::U8    },
      {Settings::DISPLAY_OFF,       "DISPLAY_OFF",       uint8_t {0},                                          uint8_t {2},                 StorageKind::U8    },
      {Settings::THEME,             "THEME",             std::string {"Default"},                              std::string {"Dark"},
       StorageKind::STRING                                                                                                                                     },
      {Settings::TEXT_SIZE,         "TEXT_SIZE",         uint8_t {TextSizePolicy::DEFAULT},
       uint8_t {TextSizePolicy::LARGE},                                                                                                     StorageKind::U8    },
      {Settings::TX_POWER,          "TX_POWER",          uint8_t {0},                                          uint8_t {2},                 StorageKind::U8    },
      {Settings::TX_ADAPTIVE,       "TX_ADAPTIVE",       false,                                                true,                        StorageKind::BOOL  },
      {Settings::GPS,               "GPS",               false,                                                true,                        StorageKind::BOOL  },
      {Settings::GPS_BAUD,          "GPS_BAUD",          uint32_t {Settings::BAUD_9600},
       uint32_t {Settings::BAUD_115200},                                                                                                    StorageKind::U32   },
      {Settings::GPS_RATE,          "GPS_RATE",          uint8_t {0},                                          uint8_t {4},                 StorageKind::U8    },
      {Settings::GPS_NMEA,          "GPS_NMEA",          false,                                                true,                        StorageKind::BOOL  },
      {Settings::GPS_CONSTEL,       "GPS_CONSTEL",       uint8_t {0},                                          uint8_t {7},                 StorageKind::U8    },
      {Settings::GPS_POWER,         "GPS_POWER",         uint8_t {0},                                          uint8_t {2},                 StorageKind::U8    },
      {Settings::GPS_DUTY,          "GPS_DUTY",          uint8_t {0},                                          uint8_t {15},                StorageKind::U8    },
      {Settings::GPS_ASSIST,        "GPS_ASSIST",        uint8_t {0},                                          uint8_t {2},                 StorageKind::U8    },
      {Settings::INTERVAL,          "INTERVAL",          defaultInterval(),                                    representativeInterval(),
       StorageKind::BLOB                                                                                                                                       },
      {Settings::MULTICONNECT,      "MULTICONNECT",      false,                                                true,                        StorageKind::BOOL  },
      {Settings::MULTISELECT,       "MULTISELECT",       defaultMultiselect(),                                 representativeMultiselect(),
       StorageKind::BLOB                                                                                                                                       },
      {Settings::RECONNECT,         "RECONNECT",         false,                                                true,                        StorageKind::BOOL  },
      {Settings::RECON_BACKOFF,     "RECON_BACKOFF",     false,                                                true,                        StorageKind::BOOL  },
      {Settings::FAUXNY,            "FAUXNY",            false,                                                true,                        StorageKind::BOOL  },
      {Settings::TOUCH_CALIBRATION, "TOUCH_CALIBRATION", defaultCalibration(),
       representativeCalibration(),                                                                                                         StorageKind::BLOB  },
      {Settings::AUTOCONNECT,       "AUTOCONNECT",       false,                                                true,                        StorageKind::BOOL  },
      {Settings::CPU_FREQ,          "CPU_FREQ",          uint8_t {Settings::CPU_FREQ_DEFAULT},                 uint8_t {240},
       StorageKind::U8                                                                                                                                         },
      {Settings::BATT_STYLE,        "BATT_STYLE",        uint8_t {Settings::BATT_STYLE_ICON},                  uint8_t {2},
       StorageKind::U8                                                                                                                                         },
      {Settings::SHOW_TITLE,        "SHOW_TITLE",        true,                                                 false,                       StorageKind::BOOL  },
      {Settings::SLEEP_CONN,        "SLEEP_CONN",        false,                                                true,                        StorageKind::BOOL  },
      {Settings::BULB,              "BULB",              SpinValue::nvs_t {30, SpinValue::UNIT_SEC},
       SpinValue::nvs_t {42, SpinValue::UNIT_MIN},                                                                                          StorageKind::BLOB  },
      {Settings::SCAN_MODE,         "SCAN_MODE",         uint8_t {0},                                          uint8_t {2},                 StorageKind::U8    },
      {Settings::SCAN_TIMEOUT,      "SCAN_TIMEOUT",      uint32_t {0},                                         uint32_t {120},              StorageKind::U32   },
      {Settings::COMPANION,         "COMPANION",         false,                                                true,                        StorageKind::BOOL  },
      {Settings::CONN_SAVER,        "CONN_SAVER",        false,                                                true,                        StorageKind::BOOL  },
      {Settings::IR,                "IR",                false,                                                true,                        StorageKind::BOOL  },
      {Settings::IR_PROTO,          "IR_PROTO",          uint8_t {0},                                          uint8_t {3},                 StorageKind::U8    },
      {Settings::FB_OUTPUT,         "FB_OUTPUT",         uint8_t {0},                                          uint8_t {4},                 StorageKind::U8    },
      {Settings::FB_EVENTS,         "FB_EVENTS",         uint8_t {0x0f},                                       uint8_t {0xa5},              StorageKind::U8    },
      {Settings::FB_VOLUME,         "FB_VOLUME",         uint8_t {64},                                         uint8_t {231},               StorageKind::U8    },
      {Settings::PRESET_PICKER,     "PRESET_PICKER",     false,                                                true,                        StorageKind::BOOL  },
      {Settings::BUTTON_MODE,       "BUTTON_MODE",       std::string {Settings::BUTTON_MODE_TWO_BUTTON_VALUE},
       std::string {Settings::BUTTON_MODE_ONE_BUTTON_VALUE},                                                                                StorageKind::STRING},
      {Settings::AUTO_OFF,          "AUTO_OFF",          uint8_t {0},                                          uint8_t {17},                StorageKind::U8    },
      {Settings::LOW_BATT,          "LOW_BATT",          uint8_t {0},                                          uint8_t {23},                StorageKind::U8    },
      {Settings::AUTO_OFF_CHARGING, "AUTO_OFF_CHARGING", false,                                                true,                        StorageKind::BOOL  },
      {Settings::SD_GPX,            "SD_GPX",            false,                                                true,                        StorageKind::BOOL  },
      {Settings::GPX_PERIOD,        "GPX_PERIOD",        uint16_t {Settings::GPX_PERIOD_DEFAULT},              uint16_t {30},
       StorageKind::U16                                                                                                                                        },
      {Settings::BOOT_SPLASH,       "BOOT_SPLASH",       true,                                                 false,                       StorageKind::BOOL  },
#if !defined(FURBLE_NO_DISPLAY)
      {Settings::DISPLAY_MODE,      "DISPLAY_MODE",      uint8_t {Settings::GUI},                              uint8_t {Settings::CONSOLE},
       StorageKind::U8                                                                                                                                         },
#endif
      {Settings::BATTERY_SAVER,     "BATTERY_SAVER",     false,                                                true,                        StorageKind::BOOL  },
#if defined(FURBLE_M5STICKS3)
      {Settings::WATCHDOG,          "WATCHDOG",          true,                                                 false,                       StorageKind::BOOL  },
#endif
  };
}

#define ASSERT_STORAGE_TYPE(setting, expected) \
  static_assert(std::is_same_v<typename Settings::storage_type<Settings::setting>::type, expected>)

ASSERT_STORAGE_TYPE(BRIGHTNESS, uint8_t);
ASSERT_STORAGE_TYPE(INACTIVITY, uint8_t);
ASSERT_STORAGE_TYPE(DISPLAY_OFF, uint8_t);
ASSERT_STORAGE_TYPE(THEME, std::string);
ASSERT_STORAGE_TYPE(TEXT_SIZE, uint8_t);
ASSERT_STORAGE_TYPE(TX_POWER, uint8_t);
ASSERT_STORAGE_TYPE(TX_ADAPTIVE, bool);
ASSERT_STORAGE_TYPE(GPS, bool);
ASSERT_STORAGE_TYPE(GPS_BAUD, uint32_t);
ASSERT_STORAGE_TYPE(GPS_RATE, uint8_t);
ASSERT_STORAGE_TYPE(GPS_NMEA, bool);
ASSERT_STORAGE_TYPE(GPS_CONSTEL, uint8_t);
ASSERT_STORAGE_TYPE(GPS_POWER, uint8_t);
ASSERT_STORAGE_TYPE(GPS_DUTY, uint8_t);
ASSERT_STORAGE_TYPE(GPS_ASSIST, uint8_t);
ASSERT_STORAGE_TYPE(INTERVAL, Furble::interval_t);
ASSERT_STORAGE_TYPE(MULTICONNECT, bool);
ASSERT_STORAGE_TYPE(MULTISELECT, Settings::multiselect_t);
ASSERT_STORAGE_TYPE(RECONNECT, bool);
ASSERT_STORAGE_TYPE(RECON_BACKOFF, bool);
ASSERT_STORAGE_TYPE(FAUXNY, bool);
ASSERT_STORAGE_TYPE(TOUCH_CALIBRATION, Settings::calibration_t);
ASSERT_STORAGE_TYPE(AUTOCONNECT, bool);
ASSERT_STORAGE_TYPE(CPU_FREQ, uint8_t);
ASSERT_STORAGE_TYPE(BATT_STYLE, uint8_t);
ASSERT_STORAGE_TYPE(SHOW_TITLE, bool);
ASSERT_STORAGE_TYPE(SLEEP_CONN, bool);
ASSERT_STORAGE_TYPE(BULB, Furble::SpinValue::nvs_t);
ASSERT_STORAGE_TYPE(SCAN_MODE, uint8_t);
ASSERT_STORAGE_TYPE(SCAN_TIMEOUT, uint32_t);
ASSERT_STORAGE_TYPE(COMPANION, bool);
ASSERT_STORAGE_TYPE(CONN_SAVER, bool);
ASSERT_STORAGE_TYPE(IR, bool);
ASSERT_STORAGE_TYPE(IR_PROTO, uint8_t);
ASSERT_STORAGE_TYPE(FB_OUTPUT, uint8_t);
ASSERT_STORAGE_TYPE(FB_EVENTS, uint8_t);
ASSERT_STORAGE_TYPE(FB_VOLUME, uint8_t);
ASSERT_STORAGE_TYPE(PRESET_PICKER, bool);
ASSERT_STORAGE_TYPE(BUTTON_MODE, std::string);
ASSERT_STORAGE_TYPE(AUTO_OFF, uint8_t);
ASSERT_STORAGE_TYPE(LOW_BATT, uint8_t);
ASSERT_STORAGE_TYPE(AUTO_OFF_CHARGING, bool);
ASSERT_STORAGE_TYPE(SD_GPX, bool);
ASSERT_STORAGE_TYPE(GPX_PERIOD, uint16_t);
ASSERT_STORAGE_TYPE(BOOT_SPLASH, bool);
#if !defined(FURBLE_NO_DISPLAY)
ASSERT_STORAGE_TYPE(DISPLAY_MODE, uint8_t);
#endif
ASSERT_STORAGE_TYPE(BATTERY_SAVER, bool);
#if defined(FURBLE_M5STICKS3)
ASSERT_STORAGE_TYPE(WATCHDOG, bool);
#endif

#undef ASSERT_STORAGE_TYPE

SettingValue loadValue(Settings::type_t type) {
  switch (type) {
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TEXT_SIZE:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
      return Settings::load<uint8_t>(type);

    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return Settings::load<uint32_t>(type);

    case Settings::GPX_PERIOD:
      return Settings::load<uint16_t>(type);

    case Settings::THEME:
    case Settings::BUTTON_MODE:
      return Settings::load<std::string>(type);

    case Settings::TX_ADAPTIVE:
    case Settings::GPS:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::COMPANION:
    case Settings::CONN_SAVER:
    case Settings::IR:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
    case Settings::AUTO_OFF_CHARGING:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return Settings::load<bool>(type);

    case Settings::INTERVAL:
      return Settings::load<Furble::interval_t>(type);
    case Settings::BULB:
      return Settings::load<Furble::SpinValue::nvs_t>(type);
    case Settings::TOUCH_CALIBRATION:
      return Settings::load<Settings::calibration_t>(type);
    case Settings::MULTISELECT:
      return Settings::load<Settings::multiselect_t>(type);
  }
  std::abort();
}

void saveValue(Settings::type_t type, const SettingValue &value) {
  std::visit(
      [type](const auto &typed_value) {
        using T = std::decay_t<decltype(typed_value)>;
        Settings::save<T>(type, typed_value);
      },
      value);
}

template <typename T>
bool equalValue(const T &left, const T &right) {
  if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
    return left == right;
  } else {
    return std::memcmp(&left, &right, sizeof(T)) == 0;
  }
}

void checkValue(const SettingCase &setting,
                const SettingValue &actual,
                const SettingValue &expected,
                const char *phase) {
  std::visit(
      [&](const auto &expected_value) {
        using T = std::decay_t<decltype(expected_value)>;
        const auto *actual_value = std::get_if<T>(&actual);
        check(actual_value != nullptr,
              std::string(phase) + " " + setting.name + " returned the wrong C++ type");
        if (actual_value != nullptr) {
          check(equalValue(*actual_value, expected_value),
                std::string(phase) + " " + setting.name + " value mismatch");
        }
      },
      expected);
}

nvs_test_value_type_t expectedNvsType(StorageKind storage) {
  switch (storage) {
    case StorageKind::BOOL:
    case StorageKind::U8:
      return NVS_TEST_U8;
    case StorageKind::U16:
      return NVS_TEST_U16;
    case StorageKind::U32:
      return NVS_TEST_U32;
    case StorageKind::STRING:
      return NVS_TEST_STRING;
    case StorageKind::BLOB:
      return NVS_TEST_BLOB;
  }
  std::abort();
}

void checkStoredType(const SettingCase &setting, const char *phase) {
  const auto &entry = Settings::get(setting.type);
  check(nvs_test_value_type(entry.nvs_namespace, entry.key) == expectedNvsType(setting.storage),
        std::string(phase) + " " + setting.name + " has the wrong NVS storage type");
}

void checkTableCoverage(const std::vector<SettingCase> &cases) {
  check(cases.size() == Settings::all().size(),
        "test case table covers exactly every Settings::m_Setting row");

  std::set<Settings::type_t> seen;
  for (const auto &test : cases) {
    check(seen.insert(test.type).second,
          std::string("test case table has a duplicate for ") + test.name);
    check(Settings::all().find(test.type) != Settings::all().end(),
          std::string("test case table references missing setting ") + test.name);
  }
  for (const auto &entry : Settings::all()) {
    check(seen.count(entry.first) == 1,
          std::string("settings table row has no round-trip case: ") + entry.second.key);
  }
}

void testDefaults(const std::vector<SettingCase> &cases) {
  nvs_test_reset();
  Settings::init();

  for (const auto &setting : cases) {
    checkValue(setting, loadValue(setting.type), setting.default_value, "default");
    checkStoredType(setting, "default");
  }
}

void testNvsRoundTrips(const std::vector<SettingCase> &cases) {
  for (const auto &setting : cases) {
    nvs_test_reset();
    saveValue(setting.type, setting.representative_value);
    checkStoredType(setting, "NVS round-trip");
    checkValue(setting, loadValue(setting.type), setting.representative_value, "NVS round-trip");
  }
}

void testSdRoundTrips(const std::vector<SettingCase> &cases) {
  for (const auto &setting : cases) {
    nvs_test_reset();
    saveValue(setting.type, setting.representative_value);

    std::string serialized;
    const auto &table_entry = Settings::get(setting.type);
    check(Furble::serializeSetting(table_entry, serialized),
          std::string("SD serialize succeeded for ") + setting.name);
    check(!serialized.empty(), std::string("SD serialize produced a value for ") + setting.name);

    nvs_test_reset();
    check(Furble::importSetting(table_entry, serialized),
          std::string("SD import succeeded for ") + setting.name);
    checkStoredType(setting, "SD round-trip");
    checkValue(setting, loadValue(setting.type), setting.representative_value, "SD round-trip");
  }
}

void testUnknownAndAliasedKeysAreIgnored() {
  nvs_test_reset();

  Furble::Preferences preferences;
  check(preferences.begin("M5ez", false), "opened M5ez for unknown-key setup");
  check(preferences.put<uint8_t>("bright", 241) == 1, "stored unknown alias key");
  check(preferences.put<uint8_t>("brightness_old", 242) == 1, "stored unknown legacy key");
  preferences.end();

  check(preferences.begin("furble", false), "opened furble for wrong-namespace setup");
  check(preferences.put<uint8_t>("brightness", 243) == 1,
        "stored canonical-looking key in the wrong namespace");
  preferences.end();

  Settings::init();
  check(Settings::load<Settings::BRIGHTNESS>() == static_cast<uint8_t>(128),
        "unknown and aliased keys do not replace the canonical default");
  check(nvs_test_value_type("M5ez", "bright") == NVS_TEST_U8,
        "unknown alias remains isolated in the mock store");
  check(nvs_test_value_type("M5ez", "brightness_old") == NVS_TEST_U8,
        "unknown legacy key remains isolated in the mock store");
  check(nvs_test_value_type("furble", "brightness") == NVS_TEST_U8,
        "wrong-namespace key remains isolated in the mock store");
}

}  // namespace

int main() {
  const auto cases = settingCases();
  checkTableCoverage(cases);
  testDefaults(cases);
  testNvsRoundTrips(cases);
  testSdRoundTrips(cases);
  testUnknownAndAliasedKeysAreIgnored();

  if (failures != 0) {
    std::cerr << "settings NVS/SD round-trip tests: " << failures << " FAILED\n";
    return 1;
  }

  std::cout << "settings NVS/SD round-trip tests: PASS (" << cases.size()
            << " settings; defaults, typed NVS, SD serialize/import, unknown keys)\n";
  return 0;
}
