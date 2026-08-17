#include "FurbleConsole.h"

#if defined(FURBLE_CONSOLE)

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <esp_console.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/uart.h>
#if defined(FURBLE_M5STICKS3) || defined(FURBLE_USB_CONSOLE)
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

#include <M5Unified.h>
#include <TinyGPS++.h>

#include "CameraList.h"
#include "Scan.h"

#include "Camera.h"
#include "FurbleBtDebug.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"

namespace Furble {

namespace {

constexpr const char *PROMPT = "furble> ";
constexpr size_t MAX_LINE = 128;
constexpr uint32_t TASK_STACK = 8192;

// Below the control task (4) and the per camera target tasks (3).
constexpr UBaseType_t TASK_PRIORITY = 2;

/** GPS receiver UART, mirrors Furble::GPS::m_UART. */
constexpr uart_port_t GPS_UART = UART_NUM_2;

/** UART carrying the ESP-IDF log, and the console on the ESP32 boards. */
constexpr uart_port_t LOG_UART = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);

/** Mirror incoming NMEA to the console. */
bool g_GPSRaw = false;

struct power_log_t {
  bool active = false;
  uint64_t intervalUs = 0;
  uint64_t nextUs = 0;
  uint64_t startUs = 0;
  float startLevel = 0;
};

power_log_t g_PowerLog;

/** Report a command result the same way every command does. */
int fail(const char *message) {
  printf("error: %s\n", message);
  return 1;
}

const char *boolStr(bool value) {
  return value ? "true" : "false";
}

/** Parse 'on', 'off', 'true', 'false', '1' and '0'. */
bool parseBool(const char *text, bool &value) {
  if (!strcasecmp(text, "on") || !strcasecmp(text, "true") || !strcmp(text, "1")) {
    value = true;
    return true;
  }
  if (!strcasecmp(text, "off") || !strcasecmp(text, "false") || !strcmp(text, "0")) {
    value = false;
    return true;
  }
  return false;
}

const char *stateStr(Control::state_t state) {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "connect_failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnecting";
  }
  return "unknown";
}

/**
 * Send a camera command.
 *
 * Control::sendCommand() is a single queue send with a zero timeout, so it is
 * safe from this task. The control task only forwards commands in the active
 * state, so report the state rather than silently doing nothing.
 */
int sendCommand(Control::cmd_t cmd) {
  auto &control = Control::getInstance();
  auto state = control.getState();

  if (state != Control::STATE_ACTIVE) {
    printf("state: %s\n", stateStr(state));
    return fail("no camera connected");
  }

  if (control.sendCommand(cmd) != pdTRUE) {
    return fail("control queue full");
  }

  printf("sent: ok\n");
  return 0;
}

/** Queue an operation for the UI task, LVGL is not thread safe. */
int sendRequest(UI::Request request, int32_t arg, const char *what) {
  if (!UI::sendRequest(request, arg)) {
    return fail("ui request queue unavailable");
  }

  printf("queued: %s\n", what);
  return 0;
}

/**
 * Queue a request that prints from the UI task and wait for the output.
 *
 * The UI loop drains the queue every 5ms, so this is generous. It keeps the
 * output ahead of the next prompt without a second synchronisation primitive.
 */
int sendPrintingRequest(UI::Request request, int32_t arg) {
  if (!UI::sendRequest(request, arg)) {
    return fail("ui request queue unavailable");
  }

  vTaskDelay(pdMS_TO_TICKS(100));
  return 0;
}

/*
 * Settings.
 *
 * The console names a setting by its NVS key, which is already lower case and
 * free of spaces. Load and save are specialised per storage type, so the
 * switches below are the one place that has to change when a setting is added.
 * The compiler will not warn about a missing case, hence the default.
 */

const Settings::setting_t *findSetting(const char *key) {
  for (const auto &entry : Settings::getAll()) {
    if (!strcasecmp(entry.second.key, key)) {
      return &entry.second;
    }
  }
  return nullptr;
}

const char *settingType(Settings::type_t type) {
  switch (type) {
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::TEXT_SIZE:
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
      return "uint8";
    case Settings::GPX_PERIOD:
      return "uint16";
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
      return "enum";
#endif
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return "uint32";
    case Settings::THEME:
    case Settings::BUTTON_MODE:
      return "string";
    case Settings::TX_ADAPTIVE:
    case Settings::GPS:
    case Settings::CONN_SAVER:
    case Settings::IR:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::GPS_NMEA:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return "bool";
    case Settings::INTERVAL:
    case Settings::TOUCH_CALIBRATION:
    case Settings::BULB:
    case Settings::MULTISELECT:
      return "struct";
  }
  return "unknown";
}

/**
 * When does a setting actually take effect after a save?
 *
 * Some settings are read on every use, others are cached by the UI when it is
 * constructed. Saving is not applying, so say which is which.
 */
const char *appliesWhen(Settings::type_t type) {
  switch (type) {
    case Settings::GPS:
    case Settings::IR:
    case Settings::GPS_BAUD:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    case Settings::SCAN_TIMEOUT:
    case Settings::GPS_RATE:
    case Settings::GPS_NMEA:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::SLEEP_CONN:
    case Settings::TX_ADAPTIVE:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::BUTTON_MODE:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
    case Settings::SD_GPX:
    case Settings::GPX_PERIOD:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
      return "immediately";
    case Settings::CONN_SAVER:
      // Only the UI toggle applies this live. A console or companion write is
      // picked up when the next connection attempt starts.
      return "on next connect";
    default:
      return "on reboot";
  }
}

/** Print the value of a setting as a single line. */
void printValue(const char *prefix, Settings::type_t type) {
  switch (type) {
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::TEXT_SIZE:
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
      printf("%s%u\n", prefix, Settings::load<uint8_t>(type));
      break;
    case Settings::GPX_PERIOD:
      printf("%s%u\n", prefix, Settings::load<uint16_t>(type));
      break;
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
    {
      const uint8_t mode = Settings::load<uint8_t>(type);
      const char *name = "unknown";
      if (mode == Settings::GUI) {
        name = "gui";
      } else if (mode == Settings::CONSOLE) {
        name = "console";
      }
      printf("%s%s\n", prefix, name);
    } break;
#endif
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      printf("%s%lu\n", prefix, Settings::load<uint32_t>(type));
      break;
    case Settings::THEME:
    case Settings::BUTTON_MODE:
      printf("%s%s\n", prefix, Settings::load<std::string>(type).c_str());
      break;
    case Settings::GPS:
    case Settings::CONN_SAVER:
    case Settings::IR:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::GPS_NMEA:
    case Settings::TX_ADAPTIVE:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      printf("%s%s\n", prefix, boolStr(Settings::load<bool>(type)));
      break;
    default:
      printf("%s<unsupported type>\n", prefix);
      break;
  }
}

int setValue(const Settings::setting_t &setting, const char *text) {
  switch (setting.type) {
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::TEXT_SIZE:
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if ((end == text) || (value > UINT8_MAX)) {
        return fail("expected 0-255");
      }
      if ((setting.type == Settings::TEXT_SIZE) && (value > Settings::TEXT_SIZE_LARGE)) {
        return fail("expected 0 (small), 1 (normal) or 2 (large)");
      }
      if ((setting.type == Settings::GPS_ASSIST) && (value > 2)) {
        return fail("expected 0, 1 or 2");
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
    } break;

    case Settings::FB_OUTPUT:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if ((end == text) || (value > Feedback::OUTPUT_SOUND_LIGHT)) {
        return fail("expected 0-4");
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
    } break;

    case Settings::GPS_DUTY:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      bool supported = false;
      if (end != text) {
        for (const uint8_t seconds : GPS::DUTY_SECONDS) {
          if (seconds == value) {
            supported = true;
            break;
          }
        }
      }
      if (!supported) {
        return fail("expected 0, 5, 10 or 15");
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
    } break;

    case Settings::GPX_PERIOD:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if ((end == text) || (value < 1) || (value > 60)) {
        return fail("expected 1-60 seconds");
      }
      Settings::save<uint16_t>(setting.type, static_cast<uint16_t>(value));
    } break;
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
    {
      uint8_t value;
      if (!strcasecmp(text, "gui")) {
        value = Settings::GUI;
      } else if (!strcasecmp(text, "console")) {
        value = Settings::CONSOLE;
      } else {
        return fail("expected gui or console");
      }
      Settings::save<uint8_t>(setting.type, value);
      if (!UI::sendRequest(UI::Request::DISPLAY_MODE, value)) {
        return fail("ui request queue unavailable");
      }
    } break;
#endif

    case Settings::SCAN_TIMEOUT:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if (end == text) {
        return fail("expected seconds");
      }
      Settings::save<uint32_t>(setting.type, static_cast<uint32_t>(value));
    } break;

    case Settings::GPS_BAUD:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if ((value != Settings::BAUD_9600) && (value != Settings::BAUD_115200)) {
        return fail("expected 9600 or 115200");
      }
      Settings::save<uint32_t>(setting.type, static_cast<uint32_t>(value));
    } break;

    case Settings::THEME:
      Settings::save<std::string>(setting.type, std::string(text));
      break;

    case Settings::BUTTON_MODE:
      if (strcasecmp(text, Settings::BUTTON_MODE_TWO_BUTTON_VALUE)
          && strcasecmp(text, Settings::BUTTON_MODE_ONE_BUTTON_VALUE)) {
        return fail("expected two-button or one-button");
      }
      Settings::save<std::string>(setting.type,
                                  strcasecmp(text, Settings::BUTTON_MODE_ONE_BUTTON_VALUE)
                                      ? Settings::BUTTON_MODE_TWO_BUTTON_VALUE
                                      : Settings::BUTTON_MODE_ONE_BUTTON_VALUE);
      break;

    case Settings::GPS:
    case Settings::CONN_SAVER:
    case Settings::IR:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::GPS_NMEA:
    case Settings::TX_ADAPTIVE:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
    {
      if ((setting.type == Settings::SD_GPX) && !SD::getInstance().isSupported()) {
        return fail("no SD card slot on this board");
      }

      bool value = false;
      if (!parseBool(text, value)) {
        return fail("expected on or off");
      }
      Settings::save<bool>(setting.type, value);
    } break;

    default:
      return fail("unsupported type");
  }

  // The GPS receiver has to be told about its own settings.
  if ((setting.type == Settings::GPS) || (setting.type == Settings::GPS_BAUD)
      || (setting.type == Settings::GPS_POWER) || (setting.type == Settings::GPS_DUTY)
      || (setting.type == Settings::GPS_RATE) || (setting.type == Settings::GPS_NMEA)
      || (setting.type == Settings::GPS_CONSTEL) || (setting.type == Settings::GPS_ASSIST)) {
    UI::sendRequest(UI::Request::GPS_RELOAD, 0);
  }
  if ((setting.type == Settings::SD_GPX) || (setting.type == Settings::GPX_PERIOD)) {
#if defined(FURBLE_NO_DISPLAY)
    // No UI task headless, reload the GPX log settings in place.
    GPS::getInstance().reloadLogSettings();
#else
    UI::sendRequest(UI::Request::SD_RELOAD, 0);
#endif
  }

  // The UI caches the IR menu visibility, so it has to be told as well.
  if (setting.type == Settings::IR) {
    UI::sendRequest(UI::Request::IR_RELOAD, 0);
  }

  // The feedback cache reloads on the UI task. FB_OUTPUT is deliberately
  // excluded, output changes apply on restart only.
  if ((setting.type == Settings::FB_EVENTS) || (setting.type == Settings::FB_VOLUME)) {
    UI::sendRequest(UI::Request::FEEDBACK_RELOAD, 0);
  }

  // The UI caches the power policies, tell it to re-read them. The headless
  // build runs no auto off or low battery policy loop, so there is nothing to
  // reload there.
#if !defined(FURBLE_NO_DISPLAY)
  if ((setting.type == Settings::AUTO_OFF) || (setting.type == Settings::LOW_BATT)) {
    UI::sendRequest(UI::Request::POWER_RELOAD, 0);
  }
#endif

  printf("saved: %s\n", setting.key);
  printf("applies: %s\n", appliesWhen(setting.type));
  return 0;
}

/** Print every setting as 'key: value', shared by 'settings list' and 'debug settings'. */
void printAllSettings(void) {
  for (const auto &entry : Settings::getAll()) {
    std::string prefix = std::string(entry.second.key) + ": ";
    printValue(prefix.c_str(), entry.second.type);
  }
}

int cmdSettings(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: settings list | get <name> | set <name> <value>");
  }

  if (!strcmp(argv[1], "list")) {
    printAllSettings();
    return 0;
  }

  if (argc < 3) {
    return fail("missing setting name");
  }

#if defined(FURBLE_NO_DISPLAY)
  if (!strcasecmp(argv[2], "display_mode")) {
    return fail("not supported in this build");
  }
#endif

  const auto *setting = findSetting(argv[2]);
  if (setting == nullptr) {
    return fail("no such setting");
  }

  if (!strcmp(argv[1], "get")) {
    printf("key: %s\n", setting->key);
    printf("name: %s\n", setting->name);
    printf("type: %s\n", settingType(setting->type));
    printf("applies: %s\n", appliesWhen(setting->type));
    printValue("value: ", setting->type);
    return 0;
  }

  if (!strcmp(argv[1], "set")) {
    if (argc < 4) {
      return fail("missing value");
    }
    return setValue(*setting, argv[3]);
  }

  return fail("expected list, get or set");
}

int cmdUI(int argc, char **argv) {
  if ((argc != 2) || strcmp(argv[1], "audit")) {
    return fail("usage: ui audit");
  }

#if defined(FURBLE_NO_DISPLAY)
  // The UI audit inspects the LVGL widget tree, which the headless build has no.
  return fail("not supported in this build");
#else
  return sendPrintingRequest(UI::Request::AUDIT, 0);
#endif
}

/*
 * GPS.
 *
 * These are the reason the console exists right now, so they carry a little
 * more than the rest.
 */

/**
 * Send a raw NMEA command to the GPS receiver.
 *
 * The body is everything between the leading '$' and the '*' checksum, for
 * example 'PCAS12,10'. This is a deliberately self contained transmit path so
 * it does not collide with the receiver configuration work happening inside
 * Furble::GPS.
 */
int gpsSend(const char *body) {
  char sentence[MAX_LINE];
  uint8_t checksum = 0;

  for (const char *c = body; *c != '\0'; c++) {
    checksum ^= static_cast<uint8_t>(*c);
  }

  int length = snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body, checksum);
  if ((length < 0) || (static_cast<size_t>(length) >= sizeof(sentence))) {
    return fail("command too long");
  }

  int written = uart_write_bytes(GPS_UART, sentence, length);
  if (written != length) {
    return fail("uart write failed");
  }

  // Strip the line ending so the echo is one line.
  sentence[length - 2] = '\0';
  printf("sent: %s\n", sentence);
  printf("bytes: %d\n", written);
  return 0;
}

bool parseHexByte(const char *text, uint8_t &value) {
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 16);
  if ((end == text) || (*end != '\0') || (parsed > UINT8_MAX)) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

int gpsBinarySend(int argc, char **argv) {
  if (argc < 3) {
    return fail("usage: gps binary <class hex> <id hex> [payload bytes]");
  }

  uint8_t class_id;
  uint8_t message_id;
  if (!parseHexByte(argv[1], class_id) || !parseHexByte(argv[2], message_id)) {
    return fail("class and id must be hex bytes");
  }

  std::vector<uint8_t> payload;
  for (int i = 3; i < argc; i++) {
    uint8_t byte;
    if (!parseHexByte(argv[i], byte)) {
      return fail("payload must contain hex bytes");
    }
    payload.push_back(byte);
  }
  if ((payload.size() % 4) != 0) {
    return fail("payload length must be a multiple of four");
  }

  if (!GPS::getInstance().sendBinary(class_id, message_id, payload)) {
    return fail("binary uart write failed");
  }
  printf("sent: binary %02X %02X, payload %u\n", class_id, message_id,
         static_cast<unsigned>(payload.size()));
  return 0;
}

int gpsAid(void) {
  if (!GPS::getInstance().sendAidIni()) {
    return fail("no valid cached fix or assistance is off");
  }
  printf("sent: AID-INI\n");
  return 0;
}

int gpsConfig(void) {
  const auto status = GPS::getInstance().getConfigStatus();
  if (status.empty()) {
    printf("config: empty\n");
    return 0;
  }
  for (const auto &entry : status) {
    printf("config: class=%02X id=%02X state=%s attempts=%u\n", entry.class_id, entry.message_id,
           GPS::configStateName(entry.state), entry.attempts);
  }
  return 0;
}

int gpsStatus(void) {
  auto &gps = GPS::getInstance();
  auto &tiny = gps.get();
  bool fix =
      tiny.location.isValid() && (tiny.location.FixQuality() != TinyGPSLocation::Quality::Invalid);

  printf("enabled: %s\n", boolStr(gps.isEnabled()));
  printf("fix: %s\n", boolStr(fix));
  printf("satellites: %lu\n", tiny.satellites.value());
  printf("lat: %.5f\n", tiny.location.lat());
  printf("lon: %.5f\n", tiny.location.lng());
  printf("alt: %.2f\n", tiny.altitude.meters());
  printf("age: %lu\n", tiny.location.age());
  printf("date: %04u-%02u-%02u\n", tiny.date.year(), tiny.date.month(), tiny.date.day());
  printf("time: %02u:%02u:%02u\n", tiny.time.hour(), tiny.time.minute(), tiny.time.second());
  printf("chars: %lu\n", tiny.charsProcessed());
  printf("sentences: %lu\n", tiny.passedChecksum());
  printf("failed: %lu\n", tiny.failedChecksum());
  printf("raw: %s\n", boolStr(g_GPSRaw));
  return 0;
}

int cmdGPS(int argc, char **argv) {
  if (argc < 2) {
    return gpsStatus();
  }

  bool value = false;

  if (parseBool(argv[1], value)) {
    Settings::save<Settings::GPS>(value);
    return sendRequest(UI::Request::GPS_RELOAD, 0, value ? "gps on" : "gps off");
  }

  if (!strcmp(argv[1], "raw")) {
    if ((argc < 3) || !parseBool(argv[2], value)) {
      return fail("usage: gps raw on | off");
    }
    g_GPSRaw = value;
    printf("raw: %s\n", boolStr(g_GPSRaw));
    return 0;
  }

  if (!strcmp(argv[1], "send")) {
    if (argc < 3) {
      return fail("usage: gps send <body>, for example gps send PCAS12,10");
    }
    return gpsSend(argv[2]);
  }

  if (!strcmp(argv[1], "binary")) {
    return gpsBinarySend(argc - 1, argv + 1);
  }

  if (!strcmp(argv[1], "config")) {
    return gpsConfig();
  }

  if (!strcmp(argv[1], "aid")) {
    return gpsAid();
  }

  if (!strcmp(argv[1], "power")) {
    if ((argc < 3) || !parseBool(argv[2], value)) {
      return fail("usage: gps power on | off");
    }
    return sendRequest(UI::Request::GPS_POWER, value, value ? "gps power on" : "gps power off");
  }

  return fail("expected on, off, raw, send, binary, config, aid or power");
}

void cmdPowerStats(void) {
  auto &power = Power::getInstance();

  for (size_t n = 0; n < Power::LOCK_COUNT; n++) {
    const auto type = static_cast<Power::LockType>(n);
    const auto stats = power.getStats(type);
    const char *name = power.getName(type);

    printf("lock.%s.held: %lu\n", name, static_cast<unsigned long>(stats.count));
    printf("lock.%s.acquires: %lu\n", name, static_cast<unsigned long>(stats.totalAcquires));
    printf("lock.%s.held_ms: %llu\n", name,
           static_cast<unsigned long long>(stats.totalHeldUs / 1000));

    for (const auto &owner : stats.owners) {
      if ((owner.owner != nullptr) && (owner.acquires > 0)) {
        printf("lock.%s.owner.%s: %lu\n", name, owner.owner,
               static_cast<unsigned long>(owner.acquires));
      }
    }
  }

  Platform::getInstance().dumpPMLocks();
}

float powerLogDrainPercentPerHour(const Platform::battery_sample_t &sample, uint64_t nowUs) {
  const uint64_t elapsedUs = nowUs - g_PowerLog.startUs;
  if (elapsedUs == 0) {
    return 0;
  }

  const float elapsedHours = static_cast<float>(elapsedUs) / 3600000000.0f;
  return (g_PowerLog.startLevel - sample.meanLevel) / elapsedHours;
}

void printPowerLogLine(uint64_t nowUs) {
  auto &platform = Platform::getInstance();
  const auto &caps = platform.getBatteryCaps();
  const auto sample = platform.getBatterySample();
  const auto pm = platform.getPMConfig();

  printf("powerlog: %llu,", static_cast<unsigned long long>(nowUs / 1000000ULL));
  if (caps.voltage) {
    printf("%u,", static_cast<unsigned>(sample.battery.voltage));
  } else {
    printf("na,");
  }
  printf("%u,", static_cast<unsigned>(sample.displayLevel));

  bool runtimeKnown = false;
  float runtimeMinutes = 0;
  if (caps.current) {
    const float drainMa = sample.meanCurrent;
    printf("%ld,%.1f,%.1f,", static_cast<long>(sample.battery.current), sample.meanCurrent,
           drainMa);
    if (!sample.battery.charging && (drainMa < -1.0f)) {
      const float remainingMah = platform.getBatteryCapacity() * (sample.meanLevel / 100.0f);
      runtimeMinutes = (remainingMah / -drainMa) * 60.0f;
      runtimeKnown = true;
    }
  } else {
    const float drainPercent = powerLogDrainPercentPerHour(sample, nowUs);
    printf("na,na,%.2f,", drainPercent);
    if (drainPercent > 0) {
      runtimeMinutes = (sample.meanLevel / drainPercent) * 60.0f;
      runtimeKnown = true;
    }
  }

  if (runtimeKnown) {
    printf("%.1f,", runtimeMinutes);
  } else {
    printf("na,");
  }
  printf("%u\n", static_cast<unsigned>(pm.max_freq_mhz));
}

void powerLogTick(void) {
  if (!g_PowerLog.active) {
    return;
  }

  const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
  if (nowUs < g_PowerLog.nextUs) {
    return;
  }

  g_PowerLog.nextUs = nowUs + g_PowerLog.intervalUs;
  printPowerLogLine(nowUs);
  fflush(stdout);
}

int cmdPower(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: power stats | log <seconds> | log off");
  }

  if (!strcmp(argv[1], "stats")) {
    if (argc != 2) {
      return fail("usage: power stats");
    }
    cmdPowerStats();
    return 0;
  }

  if (!strcmp(argv[1], "log")) {
    if (argc < 3) {
      return fail("usage: power log <seconds> | off");
    }

    if (!strcmp(argv[2], "off")) {
      if (argc != 3) {
        return fail("usage: power log <seconds> | off");
      }
      g_PowerLog.active = false;
      printf("powerlog: off\n");
      return 0;
    }

    if (argc != 3) {
      return fail("usage: power log <seconds> | off");
    }

    char *end = nullptr;
    unsigned long seconds = strtoul(argv[2], &end, 10);
    if ((end == argv[2]) || (*end != '\0') || (seconds == 0) || (seconds > 86400)) {
      return fail("expected an interval from 1 to 86400 seconds or off");
    }

    const auto sample = Platform::getInstance().getBatterySample();
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    g_PowerLog.active = true;
    g_PowerLog.intervalUs = static_cast<uint64_t>(seconds) * 1000000ULL;
    g_PowerLog.nextUs = nowUs + g_PowerLog.intervalUs;
    g_PowerLog.startUs = nowUs;
    g_PowerLog.startLevel = sample.meanLevel;

    const auto &caps = Platform::getInstance().getBatteryCaps();
    if (caps.current) {
      printf(
          "powerlog: timestamp_s,voltage_mv,level_pct,current_ma,current_ewma_ma,"
          "drain_ma,runtime_min,cpu_freq_mhz\n");
    } else {
      printf(
          "powerlog: timestamp_s,voltage_mv,level_pct,current_ma,current_ewma_ma,"
          "drain_pct_per_h,runtime_min,cpu_freq_mhz\n");
    }
    printf("powerlog: interval_s: %lu\n", seconds);
    return 0;
  }

  return fail("expected stats or log");
}

constexpr size_t MAX_TASK_SNAPSHOT = 24;

int cmdPerfTasks(void) {
  TaskStatus_t before[MAX_TASK_SNAPSHOT] = {};
  TaskStatus_t after[MAX_TASK_SNAPSHOT] = {};
  uint32_t beforeTotal = 0;
  uint32_t afterTotal = 0;

  // uxTaskGetSystemState() returns 0 when the array is too small to hold every
  // task, so a zero count while tasks exist means the snapshot overflowed. The
  // old count >= MAX_TASK_SNAPSHOT guard never fired in that case and the
  // command silently reported a count of 0 instead of an error.
  const UBaseType_t beforeCount = uxTaskGetSystemState(before, MAX_TASK_SNAPSHOT, &beforeTotal);
  if (beforeCount == 0 && uxTaskGetNumberOfTasks() > 0) {
    return fail("more than 24 tasks, increase the perf snapshot size");
  }

  vTaskDelay(pdMS_TO_TICKS(1000));

  const UBaseType_t afterCount = uxTaskGetSystemState(after, MAX_TASK_SNAPSHOT, &afterTotal);
  if (afterCount == 0 && uxTaskGetNumberOfTasks() > 0) {
    return fail("more than 24 tasks, increase the perf snapshot size");
  }

  const uint32_t totalDelta = afterTotal - beforeTotal;
  printf("perf.tasks.window_ms: 1000\n");
  printf("perf.tasks.count: %u\n", static_cast<unsigned>(afterCount));

  for (UBaseType_t n = 0; n < afterCount; n++) {
    const TaskStatus_t *old = nullptr;
    for (UBaseType_t previous = 0; previous < beforeCount; previous++) {
      if (before[previous].xTaskNumber == after[n].xTaskNumber) {
        old = &before[previous];
        break;
      }
    }
    if (old == nullptr) {
      continue;
    }

    const uint32_t runtimeDelta = after[n].ulRunTimeCounter - old->ulRunTimeCounter;
    const double cpu = (totalDelta == 0) ? 0.0 : (runtimeDelta * 100.0) / totalDelta;
    const char *name = (after[n].pcTaskName != nullptr) ? after[n].pcTaskName : "unknown";
    const uint32_t stackBytes =
        static_cast<uint32_t>(after[n].usStackHighWaterMark * sizeof(StackType_t));

    printf("task.%s.priority: %u\n", name, static_cast<unsigned>(after[n].uxCurrentPriority));
    printf("task.%s.cpu_pct: %.1f\n", name, cpu);
    printf("task.%s.stack_high_watermark: %lu\n", name, static_cast<unsigned long>(stackBytes));
  }

  return 0;
}

void printHeapCapability(const char *name, uint32_t capabilities) {
  multi_heap_info_t info = {};
  heap_caps_get_info(&info, capabilities);

  printf("heap.%s.free: %lu\n", name, static_cast<unsigned long>(info.total_free_bytes));
  printf("heap.%s.largest_block: %lu\n", name, static_cast<unsigned long>(info.largest_free_block));
  printf("heap.%s.minimum_free: %lu\n", name, static_cast<unsigned long>(info.minimum_free_bytes));
}

int cmdPerfHeap(void) {
  printHeapCapability("internal", MALLOC_CAP_INTERNAL);
  printHeapCapability("dma", MALLOC_CAP_DMA);
#if defined(CONFIG_SPIRAM)
  printHeapCapability("spiram", MALLOC_CAP_SPIRAM);
#endif
  return 0;
}

int cmdPerfLVGL(int argc, char **argv) {
#if defined(FURBLE_NO_DISPLAY)
  // LVGL stats and the overlay only exist in the display build.
  (void)argc;
  (void)argv;
  return fail("not supported in this build");
#else
  if (argc == 2) {
    return sendPrintingRequest(UI::Request::PERF, -1);
  }

  if ((argc == 4) && !strcmp(argv[2], "overlay")) {
    bool value = false;
    if (!parseBool(argv[3], value)) {
      return fail("usage: perf lvgl overlay on | off");
    }
    return sendRequest(UI::Request::PERF, value ? 1 : 0,
                       value ? "perf lvgl overlay on" : "perf lvgl overlay off");
  }

  return fail("usage: perf lvgl | perf lvgl overlay on | off");
#endif
}

int cmdPerf(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: perf tasks | heap | lvgl [overlay on | off]");
  }

  if (!strcmp(argv[1], "tasks")) {
    return (argc == 2) ? cmdPerfTasks() : fail("usage: perf tasks");
  }
  if (!strcmp(argv[1], "heap")) {
    return (argc == 2) ? cmdPerfHeap() : fail("usage: perf heap");
  }
  if (!strcmp(argv[1], "lvgl")) {
#if !defined(CONFIG_LV_USE_PERF_MONITOR)
    return fail("LVGL performance monitor is not enabled");
#else
    return cmdPerfLVGL(argc, argv);
#endif
  }

  return fail("expected tasks, heap or lvgl");
}

/*
 * Status, cameras and camera control.
 */

/** Human readable last reset cause, for boot loop and crash diagnosis. */
const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "unknown";
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "other_wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "other";
  }
}

int cmdStatus(int argc, char **argv) {
  (void)argc;
  (void)argv;

  auto &control = Control::getInstance();

  printf("version: %s\n", FURBLE_VERSION);
  printf("state: %s\n", stateStr(control.getState()));
  printf("targets: %u\n", static_cast<unsigned>(control.getTargets().size()));
  printf("reset: %s\n", resetReasonName(esp_reset_reason()));
  printf("uptime: %llu\n", esp_timer_get_time() / 1000000ULL);
  printf("heap: %lu\n", esp_get_free_heap_size());
  printf("heap_min: %lu\n", esp_get_minimum_free_heap_size());
  printf("battery: %ld\n", static_cast<long>(M5.Power.getBatteryLevel()));
  printf("voltage: %ld\n", static_cast<long>(M5.Power.getBatteryVoltage()));
  printf("current: %ld\n", static_cast<long>(M5.Power.getBatteryCurrent()));
  return 0;
}

int cmdVersion(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("version: %s\n", FURBLE_VERSION);
  printf("idf: %s\n", esp_get_idf_version());
  return 0;
}

int cmdCameras(int argc, char **argv) {
  if ((argc >= 2) && !strcmp(argv[1], "status")) {
    const auto &targets = Control::getInstance().getTargets();
    printf("targets: %u\n", static_cast<unsigned>(targets.size()));
    for (size_t n = 0; n < targets.size(); n++) {
      const auto camera = targets[n]->getCamera();
      printf("target%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
      printf("target%u.connected: %s\n", static_cast<unsigned>(n), boolStr(camera->isConnected()));
      // Connection parameter and supervision timeout telemetry. The supervision
      // timeout bounds dead-link detection, so it is the value to watch during a
      // connection-stability soak. Units are the raw BLE ones: interval and
      // timeout in 1.25 ms and 10 ms steps respectively.
      uint16_t interval = 0;
      uint16_t latency = 0;
      uint16_t timeout = 0;
      int rssi = 0;
      if (camera->getConnParams(interval, latency, timeout, rssi)) {
        printf("target%u.conn: profile: %s interval: %u latency: %u timeout: %u rssi: %d\n",
               static_cast<unsigned>(n), Camera::connProfileName(camera->getConnProfile()),
               interval, latency, timeout, rssi);
      }
    }
    return 0;
  }

  if ((argc >= 2) && strcmp(argv[1], "list")) {
    return fail("expected list or status");
  }

  // The saved camera list is owned by the UI task, which prints it.
  return sendPrintingRequest(UI::Request::CAMERAS, 1);
}

int cmdConnect(int argc, char **argv) {
  // No index connects the multi-connect selection.
  int32_t index = -1;

  if (argc >= 2) {
    char *end = nullptr;
    long value = strtol(argv[1], &end, 0);
    if ((end == argv[1]) || (value < 0)) {
      return fail("expected a camera index from 'cameras list'");
    }
    index = static_cast<int32_t>(value);
  }

  return sendRequest(UI::Request::CONNECT, index, "connect");
}

int cmdDisconnect(int argc, char **argv) {
  (void)argc;
  (void)argv;

  return sendRequest(UI::Request::DISCONNECT, 0, "disconnect");
}

int cmdShutter(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: shutter press | release | hold <ms>");
  }

  if (!strcmp(argv[1], "press")) {
    return sendCommand(Control::CMD_SHUTTER_PRESS);
  }

  if (!strcmp(argv[1], "release")) {
    return sendCommand(Control::CMD_SHUTTER_RELEASE);
  }

  if (!strcmp(argv[1], "hold")) {
    if (argc < 3) {
      return fail("usage: shutter hold <ms>");
    }
    char *end = nullptr;
    long ms = strtol(argv[2], &end, 0);
    if ((end == argv[2]) || (ms < 0) || (ms > 60000)) {
      return fail("expected 0-60000 ms");
    }
    // Pairing press with release here means a script that dies mid sequence
    // cannot leave the shutter held.
    int ret = sendCommand(Control::CMD_SHUTTER_PRESS);
    if (ret != 0) {
      return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
    return sendCommand(Control::CMD_SHUTTER_RELEASE);
  }

  return fail("expected press, release or hold");
}

int cmdIR(int argc, char **argv) {
  if ((argc < 2) || strcmp(argv[1], "fire")) {
    return fail("usage: ir fire [protocol]");
  }

  auto &ir = IR::getInstance();
  if (!ir.isSupported()) {
    return fail("no ir emitter on this board");
  }
  if (!Settings::load<Settings::IR>()) {
    return fail("ir is disabled, try 'settings set ir on'");
  }

  if (argc >= 3) {
    char *end = nullptr;
    unsigned long value = strtoul(argv[2], &end, 0);
    if ((end == argv[2]) || (value > static_cast<uint8_t>(IR::protocol_t::CANON_DELAYED))) {
      return fail("expected a protocol from 0-3");
    }
    ir.fire(static_cast<IR::protocol_t>(value));
  } else {
    ir.fire();
  }

  printf("queued: ir fire\n");
  return 0;
}

int cmdFocus(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: focus press | release");
  }

  if (!strcmp(argv[1], "press")) {
    return sendCommand(Control::CMD_FOCUS_PRESS);
  }

  if (!strcmp(argv[1], "release")) {
    return sendCommand(Control::CMD_FOCUS_RELEASE);
  }

  return fail("expected press or release");
}

int cmdScan(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: scan start | stop | list");
  }

  if (!strcmp(argv[1], "start")) {
    return sendRequest(UI::Request::SCAN, 1, "scan start");
  }

  if (!strcmp(argv[1], "stop")) {
    return sendRequest(UI::Request::SCAN, 0, "scan stop");
  }

  if (!strcmp(argv[1], "list")) {
    printf("active: %s\n", boolStr(Scan::getInstance().isActive()));
    // Scan results land in the same list, printed by the same owner.
    return sendPrintingRequest(UI::Request::CAMERAS, 0);
  }

  return fail("expected start, stop or list");
}

bool parseBtAddress(const char *text) {
  if ((text == nullptr) || (strlen(text) != 17)) {
    return false;
  }
  for (size_t index = 0; index < 17; index++) {
    if (((index % 3) == 2) && (text[index] != ':')) {
      return false;
    }
    if (((index % 3) != 2) && !isxdigit(static_cast<unsigned char>(text[index]))) {
      return false;
    }
  }
  return true;
}

bool parseBtPairMode(const char *text, BtDebug::PairMode &mode) {
  if (!strcasecmp(text, "none")) {
    mode = BtDebug::PairMode::NONE;
    return true;
  }
  if (!strcasecmp(text, "just-works") || !strcasecmp(text, "justworks")
      || !strcasecmp(text, "bond")) {
    mode = BtDebug::PairMode::JUST_WORKS;
    return true;
  }
  if (!strcasecmp(text, "numeric-display") || !strcasecmp(text, "numeric-display-passthrough")
      || !strcasecmp(text, "numeric") || !strcasecmp(text, "passkey")) {
    mode = BtDebug::PairMode::NUMERIC_DISPLAY;
    return true;
  }
  return false;
}

bool parseBtSeconds(const char *text, uint32_t &seconds) {
  char *end = nullptr;
  unsigned long value = strtoul(text, &end, 0);
  if ((end == text) || (*end != '\0') || (value < 1) || (value > 3600)) {
    return false;
  }
  seconds = static_cast<uint32_t>(value);
  return true;
}

bool parseBtKey(const char *text, uint32_t &key) {
  if ((text == nullptr) || (strlen(text) != 6)) {
    return false;
  }
  key = 0;
  for (size_t index = 0; index < 6; index++) {
    if ((text[index] < '0') || (text[index] > '9')) {
      return false;
    }
    key = (key * 10) + static_cast<uint32_t>(text[index] - '0');
  }
  return true;
}

int cmdBt(int argc, char **argv) {
  if (argc < 2) {
    return fail(
        "usage: bt scan [seconds|all [seconds]] | explore <addr> | pair yes|no|key <digits> | "
        "journal on|off|dump [n]|clear");
  }

  if (!strcmp(argv[1], "scan")) {
    if ((argc >= 3) && !strcmp(argv[2], "stop")) {
      if (argc != 3) {
        return fail("usage: bt scan stop");
      }
      return BtDebug::stopScan() ? 0 : fail("no bt scan active");
    }

    bool duplicates = false;
    int argument = 2;
    if ((argc >= 3) && !strcmp(argv[2], "all")) {
      duplicates = true;
      argument++;
    }

    uint32_t seconds = 10;
    if (argument < argc) {
      if ((argument + 1 != argc) || !parseBtSeconds(argv[argument], seconds)) {
        return fail("expected 1-3600 seconds");
      }
    }
    return BtDebug::startScan(seconds, duplicates) ? 0 : fail("bt scan unavailable");
  }

  if (!strcmp(argv[1], "explore")) {
    if ((argc >= 3) && !strcmp(argv[2], "read")) {
      if (argc != 3) {
        return fail("usage: bt explore read");
      }
      return BtDebug::readExplore() ? 0 : fail("no connected bt explorer");
    }

    if ((argc >= 3) && !strcmp(argv[2], "stop")) {
      bool keep = false;
      if (argc == 4) {
        keep = !strcasecmp(argv[3], "keep");
        if (!keep) {
          return fail("usage: bt explore stop [keep]");
        }
      } else if (argc != 3) {
        return fail("usage: bt explore stop [keep]");
      }
      return BtDebug::stopExplore(keep) ? 0 : fail("no bt explorer active");
    }

    if ((argc < 3) || !parseBtAddress(argv[2])) {
      return fail("usage: bt explore <aa:bb:cc:dd:ee:ff> [pair mode] [keep]");
    }

    BtDebug::PairMode mode = BtDebug::PairMode::NONE;
    bool keep = false;
    for (int argument = 3; argument < argc; argument++) {
      if (!strcasecmp(argv[argument], "keep")) {
        if (keep) {
          return fail("duplicate keep");
        }
        keep = true;
        continue;
      }
      if (!strcasecmp(argv[argument], "pair")) {
        if ((argument + 1 >= argc) || !parseBtPairMode(argv[++argument], mode)) {
          return fail("expected pair none, just-works or numeric-display-passthrough");
        }
        continue;
      }
      if (!parseBtPairMode(argv[argument], mode)) {
        return fail("expected pair none, just-works or numeric-display-passthrough");
      }
    }

    return BtDebug::startExplore(argv[2], mode, keep) ? 0 : fail("bt explorer unavailable");
  }

  if (!strcmp(argv[1], "pair")) {
    if ((argc < 3) || !strcasecmp(argv[2], "yes")) {
      if (argc != 3) {
        return fail("usage: bt pair yes | no | key <6 digits>");
      }
      return BtDebug::pairConfirm(true) ? 0 : fail("no pairing confirmation pending");
    }
    if (!strcasecmp(argv[2], "no")) {
      if (argc != 3) {
        return fail("usage: bt pair yes | no | key <6 digits>");
      }
      return BtDebug::pairConfirm(false) ? 0 : fail("no pairing confirmation pending");
    }
    if (!strcasecmp(argv[2], "key")) {
      uint32_t key = 0;
      if ((argc != 4) || !parseBtKey(argv[3], key)) {
        return fail("usage: bt pair key <6 digits>");
      }
      return BtDebug::pairKey(key) ? 0 : fail("no passkey entry pending");
    }
    return fail("usage: bt pair yes | no | key <6 digits>");
  }

  if (!strcmp(argv[1], "journal")) {
    if ((argc < 3) || !strcasecmp(argv[2], "on")) {
      if (argc != 3) {
        return fail("usage: bt journal on|off|dump [n]|clear");
      }
      if (!Camera::gattJournalSetEnabled(true)) {
        return fail("journal allocation failed");
      }
      printf("journal: on\n");
      return 0;
    }
    if (!strcasecmp(argv[2], "off")) {
      if (argc != 3) {
        return fail("usage: bt journal on|off|dump [n]|clear");
      }
      Camera::gattJournalSetEnabled(false);
      printf("journal: off\n");
      return 0;
    }
    if (!strcasecmp(argv[2], "clear")) {
      if (argc != 3) {
        return fail("usage: bt journal on|off|dump [n]|clear");
      }
      Camera::gattJournalClear();
      printf("journal: clear\n");
      return 0;
    }
    if (!strcasecmp(argv[2], "dump")) {
      size_t count = 0;
      if (argc == 4) {
        char *end = nullptr;
        unsigned long value = strtoul(argv[3], &end, 0);
        if ((end == argv[3]) || (*end != '\0')) {
          return fail("usage: bt journal dump [n]");
        }
        count = static_cast<size_t>(value);
      } else if (argc != 3) {
        return fail("usage: bt journal dump [n]");
      }
      Camera::gattJournalDump(count);
      return 0;
    }
    return fail("usage: bt journal on|off|dump [n]|clear");
  }

  return fail("expected scan, explore, pair or journal");
}

int cmdLog(int argc, char **argv) {
  if (argc < 3) {
    return fail("usage: log <tag> <none|error|warn|info|debug|verbose>");
  }

  static const struct {
    const char *name;
    esp_log_level_t level;
  } levels[] = {
      {"none",    ESP_LOG_NONE   },
      {"error",   ESP_LOG_ERROR  },
      {"warn",    ESP_LOG_WARN   },
      {"info",    ESP_LOG_INFO   },
      {"debug",   ESP_LOG_DEBUG  },
      {"verbose", ESP_LOG_VERBOSE},
  };

  const size_t length = strlen(argv[2]);
  if (length == 0) {
    return fail("expected a level");
  }

  // The first level the argument is a prefix of wins, so 'v' means verbose.
  for (const auto &level : levels) {
    if (!strncasecmp(argv[2], level.name, length)) {
      esp_log_level_set(argv[1], level.level);
      printf("log: %s %s\n", argv[1], level.name);
      return 0;
    }
  }

  return fail("expected none, error, warn, info, debug or verbose");
}

int cmdFeedback(int argc, char **argv) {
  if ((argc < 3) || strcmp(argv[1], "test")) {
    return fail("usage: feedback test shutter|countdown|connect|disconnect|battery");
  }

  static const struct {
    const char *name;
    Feedback::event_t event;
  } events[] = {
      {"shutter",    Feedback::SHUTTER_FIRED},
      {"countdown",  Feedback::COUNTDOWN    },
      {"connect",    Feedback::CONNECTED    },
      {"disconnect", Feedback::DISCONNECTED },
      {"battery",    Feedback::LOW_BATTERY  },
  };

  for (const auto &entry : events) {
    if (!strcasecmp(argv[2], entry.name)) {
      // Bypasses the event mask but honors the output selection, so a host
      // script can drive every pattern during hardware verification.
      return sendRequest(UI::Request::FEEDBACK_TEST, entry.event, "feedback test");
    }
  }

  return fail("expected shutter, countdown, connect, disconnect or battery");
}

/*
 * Debug dump family.
 *
 * A single 'debug' command that dumps structured state a developer otherwise
 * cannot see from the console: the full Control state machine, per-camera BLE
 * detail, and the NimBLE client pool. The heap, tasks, power, gps and settings
 * subcommands are thin aliases onto the existing dumps so 'debug all' collects
 * every diagnostic in one paste. Everything here is behind FURBLE_CONSOLE, so
 * release builds never see it.
 */

const char *cameraTypeName(Camera::Type type) {
  switch (type) {
    case Camera::Type::FUJIFILM_BASIC:
      return "fujifilm-basic";
    case Camera::Type::CANON_EOS_SMART:
      return "canon-eos-smart";
    case Camera::Type::CANON_EOS_REMOTE:
      return "canon-eos-remote";
    case Camera::Type::MOBILE_DEVICE:
      return "mobile-device";
    case Camera::Type::FAUXNY:
      return "fauxny";
    case Camera::Type::NIKON:
      return "nikon";
    case Camera::Type::SONY:
      return "sony";
    case Camera::Type::FUJIFILM_SECURE:
      return "fujifilm-secure";
    case Camera::Type::RICOH:
      return "ricoh";
    case Camera::Type::PANASONIC_LUMIX:
      return "panasonic-lumix";
    case Camera::Type::DJI_OSMO:
      return "dji-osmo";
  }
  return "unknown";
}

int debugControl(void) {
  const auto s = Control::getInstance().getDebugState();

  printf("control.state: %s\n", stateStr(s.state));
  printf("control.targets: %u\n", static_cast<unsigned>(s.targetCount));
  printf("control.connected: %u\n", static_cast<unsigned>(s.connectedCount));
  printf("control.zombies: %u\n", static_cast<unsigned>(s.zombieCount));
  printf("control.connect_in_progress: %s\n", boolStr(s.connectInProgress));
  printf("control.connect_abort: %s\n", boolStr(s.connectAbort));
  printf("control.sleep_lock_held: %s\n", boolStr(s.sleepLockHeld));
  printf("control.infinite_reconnect: %s\n", boolStr(s.infiniteReconnect));
  printf("control.reconnect_backoff: %s\n", boolStr(s.reconnectBackoff));
  printf("control.reconnect_attempt: %lu\n", static_cast<unsigned long>(s.reconnectAttempt));
  printf("control.adaptive_active: %s\n", boolStr(s.adaptiveActive));
  printf("control.power_level: %d\n", s.userPowerLevel);
  printf("control.adaptive_power_level: %d\n", s.adaptivePowerLevel);
  printf("control.rssi_strong_samples: %u\n", static_cast<unsigned>(s.rssiStrongSamples));
  printf("control.rssi_weak_samples: %u\n", static_cast<unsigned>(s.rssiWeakSamples));
  printf("control.connecting: %s\n",
         s.connectingCamera.empty() ? "none" : s.connectingCamera.c_str());
  return 0;
}

/** Print the deep BLE detail for one live target. */
void debugCameraDetail(unsigned index, Camera *camera) {
  printf("camera%u.name: %s\n", index, camera->getName().c_str());
  printf("camera%u.address: %s\n", index, camera->getAddress().toString().c_str());
  printf("camera%u.type: %s\n", index, cameraTypeName(camera->getType()));
  printf("camera%u.connected: %s\n", index, boolStr(camera->isConnected()));
  printf("camera%u.active: %s\n", index, boolStr(camera->isActive()));
  printf("camera%u.progress: %u\n", index, static_cast<unsigned>(camera->getConnectProgress()));
  printf("camera%u.profile: %s\n", index, Camera::connProfileName(camera->getConnProfile()));

  // Cached snapshot only, so this never blocks on the HCI transport from the
  // console task, matching 'cameras status'.
  uint16_t interval = 0;
  uint16_t latency = 0;
  uint16_t timeout = 0;
  int rssi = 0;
  if (camera->getConnParams(interval, latency, timeout, rssi)) {
    printf("camera%u.interval: %u\n", index, interval);
    printf("camera%u.latency: %u\n", index, latency);
    printf("camera%u.timeout: %u\n", index, timeout);
    printf("camera%u.rssi: %d\n", index, rssi);
  } else {
    printf("camera%u.conn: no cached parameters\n", index);
  }
}

int debugCamera(int argc, char **argv) {
  const auto targets = Control::getInstance().getTargets();
  printf("cameras.targets: %u\n", static_cast<unsigned>(targets.size()));

  // No index dumps every target.
  if (argc < 3) {
    for (size_t n = 0; n < targets.size(); n++) {
      debugCameraDetail(static_cast<unsigned>(n), targets[n]->getCamera().get());
    }
    return 0;
  }

  char *end = nullptr;
  long index = strtol(argv[2], &end, 0);
  if ((end == argv[2]) || (*end != '\0') || (index < 0)
      || (static_cast<size_t>(index) >= targets.size())) {
    return fail("expected a target index from 'debug camera'");
  }

  debugCameraDetail(static_cast<unsigned>(index),
                    targets[static_cast<size_t>(index)]->getCamera().get());
  return 0;
}

int debugBle(void) {
  const bool initialised = NimBLEDevice::isInitialized();
  printf("ble.initialized: %s\n", boolStr(initialised));
  if (initialised) {
    printf("ble.address: %s\n", NimBLEDevice::getAddress().toString().c_str());
    printf("ble.tx_power_dbm: %d\n", NimBLEDevice::getPower());
  }

  // The pool is fixed size. A created count that climbs across failed connects
  // and never falls is the client leak that exhausts the pool and breaks every
  // later connect until reboot.
  printf("ble.clients_created: %u\n", static_cast<unsigned>(NimBLEDevice::getCreatedClientCount()));
#if defined(CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
  printf("ble.clients_max: %d\n", CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
#endif

  // Cross-reference each control target against the live NimBLE client list.
  const auto targets = Control::getInstance().getTargets();
  for (size_t n = 0; n < targets.size(); n++) {
    auto camera = targets[n]->getCamera();
    const bool live = NimBLEDevice::getClientByPeerAddress(camera->getAddress()) != nullptr;
    printf("ble.target%u: %s client=%s\n", static_cast<unsigned>(n),
           camera->getAddress().toString().c_str(), boolStr(live));
  }
  return 0;
}

int cmdDebug(int argc, char **argv) {
  if (argc < 2) {
    return fail(
        "usage: debug control | camera [idx] | ble | heap | tasks | power | gps | "
        "settings | all");
  }

  if (!strcmp(argv[1], "control")) {
    return debugControl();
  }
  if (!strcmp(argv[1], "camera")) {
    return debugCamera(argc, argv);
  }
  if (!strcmp(argv[1], "ble")) {
    return debugBle();
  }
  if (!strcmp(argv[1], "heap")) {
    return cmdPerfHeap();
  }
  if (!strcmp(argv[1], "tasks")) {
    return cmdPerfTasks();
  }
  if (!strcmp(argv[1], "power")) {
    cmdPowerStats();
    return 0;
  }
  if (!strcmp(argv[1], "gps")) {
    return gpsStatus();
  }
  if (!strcmp(argv[1], "settings")) {
    printAllSettings();
    return 0;
  }
  if (!strcmp(argv[1], "all")) {
    printf("== status ==\n");
    cmdStatus(0, nullptr);
    printf("== control ==\n");
    debugControl();
    printf("== cameras ==\n");
    debugCamera(2, argv);
    printf("== ble ==\n");
    debugBle();
    printf("== power ==\n");
    cmdPowerStats();
    printf("== heap ==\n");
    cmdPerfHeap();
    printf("== gps ==\n");
    gpsStatus();
    printf("== settings ==\n");
    printAllSettings();
    return 0;
  }

  return fail("expected control, camera, ble, heap, tasks, power, gps, settings or all");
}

int cmdReboot(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("reboot: now\n");
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(100));
  Platform::getInstance().restart();
  return 0;
}

/** Build a command table entry, argtable is unused, every command parses argv. */
constexpr esp_console_cmd_t command(const char *name,
                                    const char *help,
                                    esp_console_cmd_func_t func) {
  return {
      .command = name,
      .help = help,
      .hint = NULL,
      .func = func,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
}

const esp_console_cmd_t COMMANDS[] = {
    command("version", "Firmware and IDF version", cmdVersion),
    command("status", "State, targets, uptime, heap and battery", cmdStatus),
    command("power", "power stats | log <seconds> | log off", cmdPower),
    command("perf", "perf tasks | heap | lvgl [overlay on | off]", cmdPerf),
    command("gps", "gps [on|off|raw|send|binary|config|aid|power]", cmdGPS),
    command("settings", "settings list | get <name> | set <name> <value>", cmdSettings),
    command("ui", "ui audit", cmdUI),
    command("cameras", "cameras list | status", cmdCameras),
    command("connect", "connect [index], no index uses the multi-connect selection", cmdConnect),
    command("disconnect", "Disconnect all cameras", cmdDisconnect),
    command("shutter", "shutter press | release | hold <ms>", cmdShutter),
    command("ir", "ir fire [protocol], 0 Nikon, 1 Sony, 2 Canon, 3 Canon 2s", cmdIR),
    command("focus", "focus press | release", cmdFocus),
    command("scan", "scan start | stop | list", cmdScan),
    command("bt",
            "bt scan|explore|pair|journal; passive sniffing of third-party links is impossible "
            "with NimBLE, this covers the active onboarding workflow",
            cmdBt),
    command("feedback",
            "feedback test <shutter|countdown|connect|disconnect|battery>",
            cmdFeedback),
    command("log", "log <tag> <level>, '*' sets all tags", cmdLog),
    command("debug",
            "debug control | camera [idx] | ble | heap | tasks | power | gps | settings | all",
            cmdDebug),
    command("reboot", "Restart the device", cmdReboot),
};

/*
 * Transport.
 *
 * The committed sdkconfig files stay untouched, which decides how the console
 * reaches the host on each board.
 *
 * On the StickS3 the USB-C port is the USB-Serial/JTAG peripheral, but
 * sdkconfig.m5stick-s3 selects UART0 as the primary console and mirrors output
 * to USB-Serial/JTAG as a secondary. A secondary console is output only, and
 * esp_console_new_repl_usb_serial_jtag() is compiled only when
 * CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG makes it primary. So read the peripheral
 * through its own driver and let printf() reach the host on the mirror.
 *
 * On the ESP32 boards the host is on the far side of a USB-UART bridge and the
 * console is already UART0, so read UART0 through the same driver ESP-IDF's own
 * REPL installs.
 *
 * Both paths feed whole lines to esp_console_run(). Dropping linenoise costs
 * line editing and history and gains a stream a host script can parse.
 */

void startTransport(void) {
#if defined(FURBLE_M5STICKS3) || defined(FURBLE_USB_CONSOLE)
  usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
  // Move the secondary console output onto the driver we just installed.
  usb_serial_jtag_vfs_use_driver();
#else
  // Same call ESP-IDF's UART REPL makes. The log keeps its existing transmit
  // path, only receive moves to the driver.
  ESP_ERROR_CHECK(uart_driver_install(LOG_UART, 256, 0, 0, NULL, 0));

  /*
   * Automatic light sleep gates the APB clock and UART0 receive drops
   * characters while it is gated. Hold an APB lock for as long as the console
   * exists.
   *
   * This defeats light sleep entirely. That is only acceptable because the
   * console is never in a release build. Do not copy this pattern into one, and
   * do not trust any power measurement taken with the console compiled in.
   */
  static esp_pm_lock_handle_t lock;
  ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "furble-console", &lock));
  ESP_ERROR_CHECK(esp_pm_lock_acquire(lock));
#endif
}

int readByte(uint8_t *byte) {
#if defined(FURBLE_M5STICKS3) || defined(FURBLE_USB_CONSOLE)
  return usb_serial_jtag_read_bytes(byte, 1, pdMS_TO_TICKS(100));
#else
  return uart_read_bytes(LOG_UART, byte, 1, pdMS_TO_TICKS(100));
#endif
}

void run(const std::string &line) {
  int ret = 0;
  esp_err_t err = esp_console_run(line.c_str(), &ret);

  if (err == ESP_ERR_NOT_FOUND) {
    printf("error: unknown command, try 'help'\n");
  } else if ((err != ESP_OK) && (err != ESP_ERR_INVALID_ARG)) {
    printf("error: %s\n", esp_err_to_name(err));
  }
}

void task(void) {
  std::string line;

  printf("\nfurble console ready, type 'help'\n%s", PROMPT);
  fflush(stdout);

  while (true) {
    powerLogTick();
    uint8_t byte = 0;

    if (readByte(&byte) != 1) {
      Camera::gattJournalDrain();
      powerLogTick();
      continue;
    }

    if ((byte == '\r') || (byte == '\n')) {
      printf("\n");
      if (!line.empty()) {
        run(line);
        line.clear();
      }
      printf("%s", PROMPT);
    } else if ((byte == '\b') || (byte == 0x7f)) {
      if (!line.empty()) {
        line.pop_back();
        printf("\b \b");
      }
    } else if ((byte >= ' ') && (byte < 0x7f) && (line.size() < MAX_LINE)) {
      line.push_back(static_cast<char>(byte));
      // No linenoise, so echo what was typed.
      printf("%c", byte);
    }

    Camera::gattJournalDrain();
    fflush(stdout);
  }
}

}  // namespace

void Console::init(void) {
  startTransport();

  esp_console_config_t config = ESP_CONSOLE_CONFIG_DEFAULT();
  config.max_cmdline_length = MAX_LINE;
  ESP_ERROR_CHECK(esp_console_init(&config));
  ESP_ERROR_CHECK(esp_console_register_help_command());

  for (const auto &entry : COMMANDS) {
    ESP_ERROR_CHECK(esp_console_cmd_register(&entry));
  }

  BaseType_t err =
      xTaskCreate([](void *) { task(); }, "console", TASK_STACK, NULL, TASK_PRIORITY, NULL);
  if (err != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create console task.");
    abort();
  }
}

void Console::gpsRaw(const char *data, size_t length) {
  static std::string line;

  if (!g_GPSRaw) {
    line.clear();
    return;
  }

  for (size_t i = 0; i < length; i++) {
    char c = data[i];
    if (c == '\n') {
      printf("nmea: %s\n", line.c_str());
      line.clear();
    } else if ((c != '\r') && (line.size() < MAX_LINE)) {
      line.push_back(c);
    }
  }
}

void Console::gpsBinary(const uint8_t *data, size_t length) {
  printf("binary:");
  for (size_t i = 0; i < length; i++) {
    printf(" %02X", data[i]);
  }
  printf("\n");
}

}  // namespace Furble

#endif  // FURBLE_CONSOLE
