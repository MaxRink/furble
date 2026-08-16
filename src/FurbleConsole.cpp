#include "FurbleConsole.h"

#if defined(FURBLE_CONSOLE)

#include <cstdio>
#include <cstring>
#include <string>

#include <esp_console.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <driver/uart.h>
#if defined(FURBLE_M5STICKS3)
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

#include <M5Unified.h>
#include <TinyGPS++.h>

#include "CameraList.h"
#include "Scan.h"

#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"

namespace Furble {

namespace {

constexpr const char *PROMPT = "furble> ";
constexpr size_t MAX_LINE = 128;
constexpr uint32_t TASK_STACK = 6144;

// Below the control task (4) and the per camera target tasks (3).
constexpr UBaseType_t TASK_PRIORITY = 2;

/** GPS receiver UART, mirrors Furble::GPS::m_UART. */
constexpr uart_port_t GPS_UART = UART_NUM_2;

/** UART carrying the ESP-IDF log, and the console on the ESP32 boards. */
constexpr uart_port_t LOG_UART = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);

/** Mirror incoming NMEA to the console. */
bool g_GPSRaw = false;

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
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      return "uint8";
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return "uint32";
    case Settings::THEME:
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
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return "bool";
    case Settings::INTERVAL:
    case Settings::TOUCH_CALIBRATION:
    case Settings::BULB:
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
    case Settings::IR_PROTO:
    case Settings::SLEEP_CONN:
    case Settings::TX_ADAPTIVE:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
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
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      printf("%s%u\n", prefix, Settings::load<uint8_t>(type));
      break;
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      printf("%s%lu\n", prefix, Settings::load<uint32_t>(type));
      break;
    case Settings::THEME:
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
    case Settings::SCAN_MODE:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    {
      char *end = nullptr;
      unsigned long value = strtoul(text, &end, 0);
      if ((end == text) || (value > UINT8_MAX)) {
        return fail("expected 0-255");
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
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
    {
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
      || (setting.type == Settings::GPS_POWER) || (setting.type == Settings::GPS_DUTY)) {
    UI::sendRequest(UI::Request::GPS_RELOAD, 0);
  }

  // The UI caches the IR menu visibility, so it has to be told as well.
  if (setting.type == Settings::IR) {
    UI::sendRequest(UI::Request::IR_RELOAD, 0);
  }

  printf("saved: %s\n", setting.key);
  printf("applies: %s\n", appliesWhen(setting.type));
  return 0;
}

int cmdSettings(int argc, char **argv) {
  if (argc < 2) {
    return fail("usage: settings list | get <name> | set <name> <value>");
  }

  if (!strcmp(argv[1], "list")) {
    for (const auto &entry : Settings::getAll()) {
      std::string prefix = std::string(entry.second.key) + ": ";
      printValue(prefix.c_str(), entry.second.type);
    }
    return 0;
  }

  if (argc < 3) {
    return fail("missing setting name");
  }

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

int gpsStatus(void) {
  auto &gps = GPS::getInstance();
  auto &tiny = gps.get();
  bool fix =
      tiny.location.isValid() && (tiny.location.FixQuality() != TinyGPSLocation::Quality::Invalid);

  printf("enabled: %s\n", boolStr(gps.isEnabled()));
  printf("fix: %s\n", boolStr(fix));
  printf("satellites: %lu\n", tiny.satellites.value());
  printf("lat: %.6f\n", tiny.location.lat());
  printf("lon: %.6f\n", tiny.location.lng());
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

  if (!strcmp(argv[1], "power")) {
    if ((argc < 3) || !parseBool(argv[2], value)) {
      return fail("usage: gps power on | off");
    }
    return sendRequest(UI::Request::GPS_POWER, value, value ? "gps power on" : "gps power off");
  }

  return fail("expected on, off, raw, send or power");
}

/*
 * Status, cameras and camera control.
 */

int cmdStatus(int argc, char **argv) {
  (void)argc;
  (void)argv;

  auto &control = Control::getInstance();

  printf("version: %s\n", FURBLE_VERSION);
  printf("state: %s\n", stateStr(control.getState()));
  printf("targets: %u\n", static_cast<unsigned>(control.getTargets().size()));
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
      const auto *camera = targets[n]->getCamera();
      printf("target%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
      printf("target%u.connected: %s\n", static_cast<unsigned>(n), boolStr(camera->isConnected()));
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

int cmdReboot(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("reboot: now\n");
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_restart();
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
    command("gps", "gps [on|off|raw on|off|send <body>|power on|off]", cmdGPS),
    command("settings", "settings list | get <name> | set <name> <value>", cmdSettings),
    command("cameras", "cameras list | status", cmdCameras),
    command("connect", "connect [index], no index uses the multi-connect selection", cmdConnect),
    command("disconnect", "Disconnect all cameras", cmdDisconnect),
    command("shutter", "shutter press | release | hold <ms>", cmdShutter),
    command("ir", "ir fire [protocol], 0 Nikon, 1 Sony, 2 Canon, 3 Canon 2s", cmdIR),
    command("focus", "focus press | release", cmdFocus),
    command("scan", "scan start | stop | list", cmdScan),
    command("log", "log <tag> <level>, '*' sets all tags", cmdLog),
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
#if defined(FURBLE_M5STICKS3)
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
#if defined(FURBLE_M5STICKS3)
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
    uint8_t byte = 0;

    if (readByte(&byte) != 1) {
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

}  // namespace Furble

#endif  // FURBLE_CONSOLE
