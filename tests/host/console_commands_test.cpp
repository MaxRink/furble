// Host command suite for the developer USB console.
//
// CLAUDE.md names the console as the automation surface for this project, yet
// src/FurbleConsole.cpp was compiled by neither the host suite nor the
// simulator, so none of its 1851 lines were measured and nothing guarded the
// command table a host script depends on.
//
// This suite drives the real console the way a host script does: it calls the
// production Console::init(), which registers the production command table and
// starts the production console task, and then types lines at that task through
// the transport double. Every assertion is made on what the command printed and
// on what reached the boundary double, never on a reimplementation.
//
// What is real here: the command table and dispatcher, every command handler,
// the Control state machine and the Camera stack against MockNimBLE, Settings
// and Preferences against the in-memory NVS store, Power, and the provisioning
// decoder. What is doubled: the ESP-IDF console API, the console transport, and
// the hardware-bound subsystems (GPS receiver, BtDebug, IR, feedback, SD, the
// PMIC platform).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

// The log level API has to be reached before the mock BLE header pulls in its
// own esp_log.h, which shares the include guard and carries no level control.
#include "esp_log.h"

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

#include "protocol/ProvisionTLV.h"

#include "M5Unified.h"
#include "Scan.h"
#include "console_doubles.h"
#include "esp_console.h"

const char *LOG_TAG = "furble-host-console";

namespace {

int g_Failures = 0;
int g_Checks = 0;

bool check(bool condition, const std::string &message) {
  g_Checks++;
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

bool checkContains(const std::string &output, const std::string &needle, const std::string &what) {
  const bool found = contains(output, needle);
  if (!found) {
    std::cerr << "  (looking for \"" << needle << "\" in)\n" << output << '\n';
  }
  return check(found, what);
}

bool waitFor(const std::function<bool()> &predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

// Every top-level command the production table registers, plus the built in
// help command. A new command or a dropped registration has to update this
// list, which is the point: the automation surface is a contract.
const std::vector<std::string> EXPECTED_COMMANDS = {
    "help",     "version",   "status", "imu",      "power",   "perf",       "gps",     "time",
    "settings", "provision", "ui",     "cameras",  "connect", "disconnect", "shutter", "ir",
    "focus",    "scan",      "bt",     "feedback", "log",     "debug",      "flash",   "reboot",
};

}  // namespace

namespace {

/** One command's accepted subcommand set, and how an unknown one is rejected. */
struct SubcommandContract {
  const char *command;
  /** Text the handler prints when the subcommand is not recognised. */
  const char *rejection;
  /** Extra arguments so the parser reaches the subcommand decision. */
  const char *suffix;
  std::vector<std::string> subcommands;
};

const std::vector<SubcommandContract> SUBCOMMANDS = {
    {"power",    "expected stats or log",                                       "",         {"stats", "log"}                      },
    {"perf",     "expected tasks, heap or lvgl",                                "",         {"tasks", "heap", "lvgl"}             },
    {"gps",
     "expected on, off, raw, send, binary, config, aid or power",               "",
     {"on", "off", "raw", "send", "binary", "config", "aid", "power"}                                                             },
    {"time",     "usage: time status | flush",                                  "",         {"status", "flush"}                   },
    {"settings", "expected list, get or set",                                   " theme",   {"list", "get", "set"}                },
    {"ui",       "usage: ui audit",                                             "",         {"audit"}                             },
    {"cameras",  "expected list or status",                                     "",         {"list", "status"}                    },
    {"imu",      "usage: imu status",                                           "",         {"status"}                            },
    {"shutter",  "expected press, release or hold",                             "",         {"press", "release", "hold"}          },
    {"ir",       "usage: ir fire [protocol]",                                   "",         {"fire"}                              },
    {"focus",    "expected press or release",                                   "",         {"press", "release"}                  },
    {"scan",     "expected start, stop or list",                                "",         {"start", "stop", "list"}             },
    {"bt",       "expected scan, explore, pair or journal",                     "",         {"scan", "explore", "pair", "journal"}},
    {"feedback", "usage: feedback test",                                        " shutter", {"test"}                              },
    {"flash",    "usage: flash prepare | cancel",                               "",         {"prepare", "cancel"}                 },
    {"debug",
     "expected control, camera, ble, heap, tasks, power, gps, settings or all", "",
     {"control", "camera", "ble", "heap", "tasks", "power", "gps", "settings", "all"}                                             },
};

size_t expectedSubcommandCount(void) {
  size_t total = 0;
  for (const auto &entry : SUBCOMMANDS) {
    total += entry.subcommands.size();
  }
  return total;
}

/** A dispatched command: the console error, the handler return code, the output. */
struct Result {
  esp_err_t err;
  int rc;
  std::string out;
};

/**
 * Dispatch a line through the production esp_console_run() path.
 *
 * This is the same call the console task makes, so the tokenizer, the lookup
 * and the handler all run. It also hands back the handler return code, which
 * the task itself discards, so an error path can be checked for a non-zero rc
 * as well as its message.
 */
Result runDirect(const std::string &line) {
  const long start = ConsoleHost::captureOffset();
  int rc = -999;
  const esp_err_t err = esp_console_run(line.c_str(), &rc);
  return {err, rc, ConsoleHost::capturedSince(start)};
}

}  // namespace

namespace {

// Test 1. The registered command table is the automation contract, so it is
// asserted exactly: no missing command, no unexpected extra.
void testCommandTable(void) {
  std::cerr << "test: the registered command table matches the contract\n";

  const auto names = ConsoleHost::commandNames();
  const std::set<std::string> registered(names.begin(), names.end());
  const std::set<std::string> expected(EXPECTED_COMMANDS.begin(), EXPECTED_COMMANDS.end());

  check(names.size() == registered.size(), "no command is registered twice");
  check(registered.size() == expected.size(),
        "the table holds exactly " + std::to_string(expected.size()) + " commands, found "
            + std::to_string(registered.size()));

  for (const auto &name : expected) {
    check(registered.count(name) == 1, "command '" + name + "' is registered");
  }
  for (const auto &name : registered) {
    check(expected.count(name) == 1, "command '" + name + "' is expected");
  }

  // Every command carries help text, which is what 'help' prints.
  for (const auto &entry : ConsoleHost::commands()) {
    check(!entry.help.empty(), "command '" + entry.name + "' carries help text");
  }

  const Result help = runDirect("help");
  check(help.err == ESP_OK, "help dispatches");
  check(help.rc == 0, "help returns success");
  for (const auto &name : expected) {
    checkContains(help.out, name, "help lists '" + name + "'");
  }
}

// Test 2. Each command's documented subcommand set, asserted from three sides:
// an unknown subcommand is rejected, every documented one is not, and the usage
// text the command prints on rejection names every documented one.
//
// The handlers decide their subcommands with a chain of strcmp and expose no
// list, so this cannot detect a subcommand added to a handler and to its usage
// text but not to the table below. It does catch a removed or renamed
// subcommand, and it pins the usage text a host script reads to this table.
void testSubcommandSets(void) {
  std::cerr << "test: each command's documented subcommands are accepted and named ("
            << expectedSubcommandCount() << " total)\n";

  for (const auto &contract : SUBCOMMANDS) {
    const std::string command = contract.command;
    const std::string suffix = contract.suffix;

    const Result unknown = runDirect(command + " __nope__" + suffix);
    checkContains(unknown.out, contract.rejection, command + " rejects an unknown subcommand");
    check(unknown.rc != 0, command + " returns non-zero for an unknown subcommand");

    for (const auto &subcommand : contract.subcommands) {
      const Result accepted = runDirect(command + " " + subcommand + suffix);
      check(!contains(accepted.out, contract.rejection),
            command + " " + subcommand + " is an accepted subcommand");
      checkContains(unknown.out, subcommand,
                    command + " names '" + subcommand + "' in its usage text");
    }
  }
}

// Test 3. The state reporting commands, which are what a host script polls.
void testStatusAndVersion(void) {
  std::cerr << "test: version and status report the device state\n";
  ConsoleHost::resetDoubles();

  const Result version = runDirect("version");
  check(version.rc == 0, "version returns success");
  checkContains(version.out, "version: host", "version reports the firmware version");
  checkContains(version.out, "idf: ", "version reports the IDF version");

  Furble::Platform::battery_caps_t caps = {true, true, true, false};
  Furble::Platform::battery_sample_t sample = {
      {55, 3812, -95, false},
      55.0f, 3812.0f, -95.0f, 55
  };
  Furble::Platform::getInstance().setBatteryCaps(caps);
  Furble::Platform::getInstance().setBatterySample(sample);

  const Result status = runDirect("status");
  check(status.rc == 0, "status returns success");
  checkContains(status.out, "state: idle", "status reports the control state");
  checkContains(status.out, "targets: 0", "status reports the target count");
  checkContains(status.out, "reset: poweron", "status reports the reset reason");
  checkContains(status.out, "uptime: ", "status reports the uptime");
  checkContains(status.out, "heap: ", "status reports the free heap");
  checkContains(status.out, "battery: 55", "status reports the battery level");
  checkContains(status.out, "voltage: 3812", "status reports the battery voltage");
  checkContains(status.out, "current: -95", "status reports the battery current");

  // Every reset cause has to come back as a name a host script can match on,
  // because a boot loop is diagnosed from this line alone.
  const std::vector<std::pair<int, std::string>> resets = {
      {0,  "unknown"  },
      {1,  "poweron"  },
      {2,  "external" },
      {3,  "software" },
      {4,  "panic"    },
      {5,  "int_wdt"  },
      {6,  "task_wdt" },
      {7,  "other_wdt"},
      {8,  "deepsleep"},
      {9,  "brownout" },
      {10, "sdio"     },
      {99, "other"    },
  };
  for (const auto &reset : resets) {
    ConsoleHost::misc().resetReason = reset.first;
    checkContains(runDirect("status").out, "reset: " + reset.second,
                  "reset cause " + std::to_string(reset.first) + " reports as " + reset.second);
  }
  ConsoleHost::misc().resetReason = 1;

  // A board without battery telemetry reports the sentinel instead of a value.
  Furble::Platform::getInstance().setBatteryCaps({false, false, false, false});
  const Result blind = runDirect("status");
  checkContains(blind.out, "battery: -1", "status reports -1 without a level gauge");
  checkContains(blind.out, "voltage: -1", "status reports -1 without a voltmeter");
  checkContains(blind.out, "current: 0", "status reports 0 without a current sensor");
  Furble::Platform::getInstance().setBatteryCaps(caps);
}

// Test 4. The settings commands roundtrip a real value through the production
// Settings table and the in-memory NVS store.
void testSettings(void) {
  std::cerr << "test: settings list, get and set roundtrip through real Settings\n";
  ConsoleHost::resetDoubles();

  const Result list = runDirect("settings list");
  check(list.rc == 0, "settings list returns success");
  checkContains(list.out, "brightness: ", "settings list prints brightness");
  checkContains(list.out, "theme: ", "settings list prints the theme string");
  checkContains(list.out, "gpx_period: ", "settings list prints the GPX period");
  checkContains(list.out, "display_mode: ", "settings list prints the display mode");

  const Result get = runDirect("settings get brightness");
  check(get.rc == 0, "settings get returns success");
  checkContains(get.out, "key: brightness", "settings get names the key");
  checkContains(get.out, "name: Brightness", "settings get names the setting");
  checkContains(get.out, "type: uint8", "settings get reports the storage type");
  checkContains(get.out, "applies: on reboot", "settings get reports when it applies");
  checkContains(get.out, "value: ", "settings get prints the value");

  // The settings the console cannot render are still described, and say so
  // instead of printing something misleading.
  for (const char *key : {"interval", "bulb", "multiselect", "t_calib"}) {
    const Result structured = runDirect(std::string("settings get ") + key);
    check(structured.rc == 0, std::string("settings get ") + key + " returns success");
    checkContains(structured.out, "type: struct", std::string(key) + " reports a struct type");
    checkContains(structured.out, "value: <unsupported type>",
                  std::string(key) + " declines to render its value");
  }

  // Roundtrip a numeric setting through the real save and load path.
  const Result set = runDirect("settings set brightness 77");
  check(set.rc == 0, "settings set returns success");
  checkContains(set.out, "saved: brightness", "settings set confirms the save");
  checkContains(runDirect("settings get brightness").out, "value: 77",
                "the saved brightness reads back");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 77,
        "the value reached the real Settings store");

  // A boolean, a string, a uint16 and a uint32, each with its own parser.
  checkContains(runDirect("settings set gps on").out, "saved: gps", "a boolean setting saves");
  checkContains(runDirect("settings get gps").out, "value: true", "the boolean reads back true");
  check(ConsoleHost::ui().requests.size() >= 1, "saving gps queues a UI reload");
  checkContains(runDirect("settings set gps off").out, "applies: immediately",
                "an immediate setting says so");

  checkContains(runDirect("settings set theme Dark").out, "saved: theme", "a string setting saves");
  checkContains(runDirect("settings get theme").out, "value: Dark", "the string reads back");

  checkContains(runDirect("settings set gpx_period 30").out, "saved: gpx_period",
                "a uint16 setting saves");
  checkContains(runDirect("settings get gpx_period").out, "value: 30", "the uint16 reads back");

  // The two gesture settings. Both apply immediately and both must notify the
  // UI task, which owns the 50 Hz poll timer, without touching LVGL from here.
  {
    const unsigned before = ConsoleHost::ui().gestureNotifications;
    const Result wake = runDirect("settings get imu_wake");
    check(wake.rc == 0, "settings get imu_wake returns success");
    checkContains(wake.out, "name: Wake Gesture", "imu_wake names the setting");
    checkContains(wake.out, "type: uint8", "imu_wake reports uint8");
    checkContains(wake.out, "applies: immediately", "imu_wake applies immediately");
    checkContains(runDirect("settings set imu_wake 3").out, "saved: imu_wake",
                  "imu_wake saves in range");
    checkContains(runDirect("settings get imu_wake").out, "value: 3", "imu_wake reads back");
    check(Furble::Settings::load<uint8_t>(Furble::Settings::IMU_WAKE) == 3,
          "imu_wake reached the real Settings store");

    const Result bad = runDirect("settings set imu_wake 4");
    check(bad.rc != 0, "imu_wake rejects an out-of-range mode");
    checkContains(bad.out, "expected 0-3", "imu_wake names its range");
    check(Furble::Settings::load<uint8_t>(Furble::Settings::IMU_WAKE) == 3,
          "a rejected imu_wake leaves the stored value alone");

    const Result trig = runDirect("settings get imu_trigger");
    checkContains(trig.out, "name: Double-Tap Shutter", "imu_trigger names the setting");
    checkContains(trig.out, "type: bool", "imu_trigger reports bool");
    checkContains(trig.out, "applies: immediately", "imu_trigger applies immediately");
    checkContains(runDirect("settings set imu_trigger on").out, "saved: imu_trigger",
                  "imu_trigger saves");
    checkContains(runDirect("settings get imu_trigger").out, "value: true",
                  "imu_trigger reads back true");
    check(ConsoleHost::ui().gestureNotifications > before,
          "a gesture setting write notifies the UI task");
  }

  // The gesture amplitude calibration knob. Runtime only, clamped at both ends.
  {
    checkContains(runDirect("imu scale").out, "scale: 1.00", "imu scale defaults to 1.0");
    checkContains(runDirect("imu scale 2.5").out, "scale: 2.50", "imu scale accepts a value");
    checkContains(runDirect("imu scale").out, "scale: 2.50", "imu scale reads back");
    const Result low = runDirect("imu scale 0.1");
    check(low.rc != 0, "imu scale rejects a value below the range");
    checkContains(low.out, "expected 0.25-4.0", "imu scale names its range");
    check(runDirect("imu scale 9").rc != 0, "imu scale rejects a value above the range");
    check(runDirect("imu scale nope").rc != 0, "imu scale rejects a non-number");
    checkContains(runDirect("imu scale").out, "scale: 2.50",
                  "a rejected imu scale leaves the live value alone");
    runDirect("imu scale 1.0");
  }

  checkContains(runDirect("settings set scan_timeout 45").out, "saved: scan_timeout",
                "a uint32 setting saves");
  checkContains(runDirect("settings get scan_timeout").out, "value: 45", "the uint32 reads back");

  // Fix hold and extrapolation. The hold is the only uint8 with its own range
  // parser, and both of them have to reach the GPS reload, because a hold the
  // user set from the console that only took effect after a reboot would look
  // like the feature simply not working.
  checkContains(runDirect("settings get gps_hold").out, "type: uint8",
                "the fix hold setting is a uint8");
  checkContains(runDirect("settings get gps_hold").out, "applies: immediately",
                "the fix hold setting applies at once");
  // It renders its number rather than declining to. printValue is a separate
  // switch from settingType, so a setting can be typed and still unprintable.
  checkContains(runDirect("settings get gps_hold").out, "value: 0",
                "fix hold defaults to off and prints it");
  checkContains(runDirect("settings set gps_hold 4").out, "saved: gps_hold",
                "the fix hold setting saves");
  checkContains(runDirect("settings get gps_hold").out, "value: 4",
                "the saved fix hold reads back");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::GPS_HOLD) == 4,
        "the fix hold value reached the real Settings store");

  const Result badHold = runDirect("settings set gps_hold 5");
  check(badHold.rc != 0, "a fix hold value past the last option fails");
  checkContains(badHold.out, "expected 0-4", "the fix hold range error names the range");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::GPS_HOLD) == 4,
        "a rejected fix hold value does not overwrite the saved one");

  checkContains(runDirect("settings get gps_extrap").out, "type: bool",
                "the extrapolate setting is a bool");
  checkContains(runDirect("settings get gps_extrap").out, "applies: immediately",
                "the extrapolate setting applies at once");
  checkContains(runDirect("settings get gps_extrap").out, "value: false",
                "extrapolation defaults to off");
  checkContains(runDirect("settings set gps_extrap on").out, "saved: gps_extrap",
                "the extrapolate setting saves");
  checkContains(runDirect("settings get gps_extrap").out, "value: true",
                "the saved extrapolate setting reads back");

  const size_t beforeHoldReload = ConsoleHost::ui().requests.size();
  runDirect("settings set gps_hold 1");
  runDirect("settings set gps_extrap off");
  check(ConsoleHost::ui().requests.size() >= beforeHoldReload + 2,
        "both fix hold settings queue a GPS reload");

  checkContains(runDirect("settings set button_mode one-button").out, "saved: button_mode",
                "the button mode saves");
  checkContains(runDirect("settings set display_mode console").out, "saved: display_mode",
                "the display mode saves");
  checkContains(runDirect("settings get display_mode").out, "value: console",
                "the display mode reads back by name");
  checkContains(runDirect("settings set display_mode gui").out, "saved: display_mode",
                "the display mode returns to gui");

  // Error paths.
  const Result noArgs = runDirect("settings");
  check(noArgs.rc != 0, "settings with no subcommand fails");
  checkContains(noArgs.out, "usage: settings list | get <name> | set <name> <value>",
                "settings prints its usage");

  const Result noName = runDirect("settings get");
  check(noName.rc != 0, "settings get with no name fails");
  checkContains(noName.out, "missing setting name", "settings get names the missing argument");

  const Result unknown = runDirect("settings get __nope__");
  check(unknown.rc != 0, "an unknown setting fails");
  checkContains(unknown.out, "no such setting", "an unknown setting is named as such");

  const Result noValue = runDirect("settings set brightness");
  check(noValue.rc != 0, "settings set with no value fails");
  checkContains(noValue.out, "missing value", "settings set names the missing value");

  const Result badRange = runDirect("settings set brightness 999");
  check(badRange.rc != 0, "an out of range value fails");
  checkContains(badRange.out, "expected 0-255", "the range error names the range");

  const Result badBool = runDirect("settings set gps maybe");
  check(badBool.rc != 0, "a bad boolean fails");
  checkContains(badBool.out, "expected on or off", "the boolean error names the values");

  const Result badBaud = runDirect("settings set gps_baud 4800");
  check(badBaud.rc != 0, "an unsupported baud fails");
  checkContains(badBaud.out, "expected 9600 or 115200", "the baud error names the values");

  const Result badDuty = runDirect("settings set gps_duty 7");
  check(badDuty.rc != 0, "an unsupported duty interval fails");
  checkContains(badDuty.out, "expected 0, 5, 10 or 15", "the duty error names the values");

  const Result badPeriod = runDirect("settings set gpx_period 0");
  check(badPeriod.rc != 0, "a zero GPX period fails");
  checkContains(badPeriod.out, "expected 1-60 seconds", "the period error names the range");

  const Result badMode = runDirect("settings set display_mode neither");
  check(badMode.rc != 0, "an unknown display mode fails");
  checkContains(badMode.out, "expected gui or console", "the display mode error names the values");

  const Result badButton = runDirect("settings set button_mode three-button");
  check(badButton.rc != 0, "an unknown button mode fails");
  checkContains(badButton.out, "expected two-button or one-button", "the button error is named");

  const Result unsupported = runDirect("settings set interval 5");
  check(unsupported.rc != 0, "a struct setting cannot be set from the console");
  checkContains(unsupported.out, "unsupported type", "the struct setting says so");

  // The SD card gate: no slot means the GPX logging toggle is refused.
  ConsoleHost::misc().sdSupported = false;
  const Result noSD = runDirect("settings set sd_gpx on");
  check(noSD.rc != 0, "sd_gpx is refused without a card slot");
  checkContains(noSD.out, "no SD card slot on this board", "the SD gate explains itself");
  ConsoleHost::misc().sdSupported = true;
  checkContains(runDirect("settings set sd_gpx on").out, "saved: sd_gpx",
                "sd_gpx saves with a card slot");
  checkContains(runDirect("settings set sd_gpx off").out, "saved: sd_gpx", "sd_gpx toggles back");

  // The reload fan out: each family tells its own subsystem.
  ConsoleHost::resetDoubles();
  runDirect("settings set ir on");
  runDirect("settings set fb_events 3");
  runDirect("settings set auto_off 5");
  bool sawIR = false;
  bool sawFeedback = false;
  bool sawPower = false;
  for (const auto &request : ConsoleHost::ui().requests) {
    sawIR = sawIR || (request.request == Furble::UI::Request::IR_RELOAD);
    sawFeedback = sawFeedback || (request.request == Furble::UI::Request::FEEDBACK_RELOAD);
    sawPower = sawPower || (request.request == Furble::UI::Request::POWER_RELOAD);
  }
  check(sawIR, "an ir write asks the UI to reload the menu");
  check(sawFeedback, "a feedback write asks the UI to reload the cache");
  check(sawPower, "a power policy write asks the UI to reload");
  runDirect("settings set ir off");
}

}  // namespace

namespace {

// Test 5. The GPS command tree. The receiver is doubled at its boundary, so
// every branch of the parsing, the checksum the console computes and the
// frames it hands the receiver are all asserted.
void testGPS(void) {
  std::cerr << "test: the gps command tree parses, formats and dispatches\n";
  ConsoleHost::resetDoubles();

  auto &gps = ConsoleHost::gps();
  gps.status = {true, 9, 51.5074, -0.1278, 35.5, 250, 2026, 9, 2, 13, 45, 7, 4096, 128};
  gps.cycle = {true, 3};
  // Receiver state, the half of the picture the fix snapshot does not carry.
  // The degraded flag and retry count live here too, in one snapshot, so the
  // console cannot report a fresh cycle state beside a stale retry count.
  gps.source = Furble::GPS::SOURCE_UART;
  gps.fix = Furble::GPS::Fix::LIVE;
  gps.receiver = {"degraded", Furble::GPS::POWER_STANDBY, 10, 1000, 42000, true, 1, true, true, 3};

  const Result status = runDirect("gps");
  check(status.rc == 0, "bare gps prints the status");
  checkContains(status.out, "enabled: true", "gps status reports the setting");
  checkContains(status.out, "fix: true", "gps status reports the fix");
  checkContains(status.out, "satellites: 9", "gps status reports the satellite count");
  checkContains(status.out, "lat: 51.50740", "gps status reports the latitude");
  checkContains(status.out, "lon: -0.12780", "gps status reports the longitude");
  checkContains(status.out, "date: 2026-09-02", "gps status reports the date");
  checkContains(status.out, "time: 13:45:07", "gps status reports the time");
  checkContains(status.out, "degraded: true", "gps status reports the power cycle health");
  checkContains(status.out, "retries: 3", "gps status reports the retry count");
  checkContains(status.out, "source: uart", "gps status reports the fix source");
  checkContains(status.out, "cycle: degraded", "gps status reports the power cycle state");
  checkContains(status.out, "policy: standby", "gps status reports the receiver power policy");
  checkContains(status.out, "duty: 10", "gps status reports the standby interval");
  checkContains(status.out, "rate: 1000", "gps status reports the configured fix interval");
  checkContains(status.out, "sentence_age: 42000", "gps status reports the sentence age");
  checkContains(status.out, "assist: 1", "gps status reports the assisted start mode");
  checkContains(status.out, "assist_cache: true", "gps status reports the assist cache state");
  checkContains(status.out, "raw: false", "gps status reports the NMEA mirror");
  // The fix hold state. "fix: true" above is the receiver's own fix flag, which
  // says nothing about whether the position being sent to the camera is live or
  // held, and the GPS Data page is not reachable from a bench script.
  checkContains(status.out, "fix_state: live", "gps status reports a live fix");
  checkContains(status.out, "hold: 0", "gps status reports the fix hold bound");
  checkContains(status.out, "hold_remaining: 0", "gps status reports no held time");

  gps.fix = Furble::GPS::Fix::HELD;
  gps.holdLimitMs = 30000;
  gps.holdRemainingMs = 12000;
  const Result held = runDirect("gps");
  checkContains(held.out, "fix_state: held", "gps status reports a held fix");
  checkContains(held.out, "hold: 30000", "gps status reports the configured bound");
  checkContains(held.out, "hold_remaining: 12000", "gps status reports the time left");

  gps.fix = Furble::GPS::Fix::NONE;
  gps.holdRemainingMs = 0;
  checkContains(runDirect("gps").out, "fix_state: none",
                "gps status reports a lost fix once the hold expires");
  gps.holdLimitMs = 0;

  // A receiver that has said nothing has no age, and "none" must not read as a
  // zero second age.
  gps.receiver.have_sentence = false;
  gps.source = Furble::GPS::SOURCE_NONE;
  const Result quiet = runDirect("gps");
  check(quiet.rc == 0, "gps status runs with a silent receiver");
  checkContains(quiet.out, "sentence_age: none", "a silent receiver reports no sentence age");
  checkContains(quiet.out, "source: none", "a silent receiver reports no fix source");
  gps.receiver.have_sentence = true;
  gps.source = Furble::GPS::SOURCE_UART;

  // The raw mirror, and the Console::gpsRaw() entry point the receiver calls.
  checkContains(runDirect("gps raw on").out, "raw: true", "gps raw on reports its state");
  const long rawStart = ConsoleHost::captureOffset();
  Furble::Console::gpsRaw("$GNRMC,ignored\r\n", 16);
  checkContains(ConsoleHost::capturedSince(rawStart), "nmea: $GNRMC,ignored",
                "the raw mirror prints a complete sentence");
  checkContains(runDirect("gps raw off").out, "raw: false", "gps raw off reports its state");
  const long offStart = ConsoleHost::captureOffset();
  Furble::Console::gpsRaw("$GNRMC,ignored\r\n", 16);
  check(!contains(ConsoleHost::capturedSince(offStart), "nmea:"),
        "the raw mirror is silent when off");

  // Console::gpsBinary() prints a verified frame as hex.
  const long binaryStart = ConsoleHost::captureOffset();
  const uint8_t frame[] = {0xBA, 0xCE, 0x01};
  Furble::Console::gpsBinary(frame, sizeof(frame));
  checkContains(ConsoleHost::capturedSince(binaryStart), "binary: BA CE 01",
                "a verified binary frame prints as hex");

  // The transmit path computes its own NMEA checksum.
  const Result send = runDirect("gps send PCAS12,10");
  check(send.rc == 0, "gps send returns success");
  checkContains(send.out, "sent: $PCAS12,10*", "gps send echoes the sentence it built");
  check(ConsoleHost::uart().writes.size() == 1, "gps send reached the UART once");
  if (!ConsoleHost::uart().writes.empty()) {
    const std::string written = ConsoleHost::uart().writes.front();
    checkContains(written, "$PCAS12,10*", "the UART carries the sentence");
    checkContains(written, "\r\n", "the sentence is line terminated");
  }

  ConsoleHost::uart().shortWrite = true;
  const Result shortWrite = runDirect("gps send PCAS12,10");
  check(shortWrite.rc != 0, "a short UART write fails");
  checkContains(shortWrite.out, "uart write failed", "a short UART write is reported");
  ConsoleHost::uart().shortWrite = false;

  // The binary frame path parses hex bytes and enforces the payload length.
  const Result binary = runDirect("gps binary 06 01 01 02 03 04");
  check(binary.rc == 0, "gps binary returns success");
  checkContains(binary.out, "sent: binary 06 01, payload 4", "gps binary reports the frame");
  check(ConsoleHost::gps().binaryFrames.size() == 1, "the frame reached the receiver");

  checkContains(runDirect("gps binary zz 01").out, "class and id must be hex bytes",
                "a bad class byte is rejected");
  checkContains(runDirect("gps binary 06 01 zz 02 03 04").out, "payload must contain hex bytes",
                "a bad payload byte is rejected");
  checkContains(runDirect("gps binary 06 01 01 02").out,
                "payload length must be a multiple of four", "an odd payload length is rejected");
  checkContains(runDirect("gps binary 06").out, "usage: gps binary", "a short frame prints usage");

  ConsoleHost::gps().binaryResult = false;
  checkContains(runDirect("gps binary 06 01").out, "binary uart write failed",
                "a failed binary write is reported");
  ConsoleHost::gps().binaryResult = true;

  // Assistance injection.
  checkContains(runDirect("gps aid").out, "sent: AID-INI", "gps aid reports the injection");
  check(ConsoleHost::gps().aidCalls == 1, "gps aid reached the receiver");
  ConsoleHost::gps().aidResult = false;
  checkContains(runDirect("gps aid").out, "no valid cached fix or assistance is off",
                "a refused assistance injection is reported");
  ConsoleHost::gps().aidResult = true;

  // The binary configuration status table.
  checkContains(runDirect("gps config").out, "config: empty", "an empty config table says so");
  ConsoleHost::gps().config = {
      {0x06, 0x01, Furble::GPS::CONFIG_ACKED,   1},
      {0x06, 0x07, Furble::GPS::CONFIG_TIMEOUT, 3},
  };
  const Result config = runDirect("gps config");
  checkContains(config.out, "class=06 id=01 state=acked attempts=1",
                "the config table prints an acked entry");
  checkContains(config.out, "class=06 id=07 state=timeout attempts=3",
                "the config table prints a timed out entry");

  // The receiver rail toggle is a UI task request.
  ConsoleHost::ui().requests.clear();
  checkContains(runDirect("gps power on").out, "queued: gps power on",
                "gps power on queues a UI request");
  check(!ConsoleHost::ui().requests.empty()
            && ConsoleHost::ui().requests.back().request == Furble::UI::Request::GPS_POWER
            && ConsoleHost::ui().requests.back().arg == 1,
        "gps power on reached the UI queue with the right argument");
  checkContains(runDirect("gps power maybe").out, "usage: gps power on | off",
                "a bad rail argument prints usage");

  // A full UI queue is reported rather than silently dropped.
  ConsoleHost::ui().queueAvailable = false;
  const Result full = runDirect("gps power on");
  check(full.rc != 0, "an unavailable UI queue fails the command");
  checkContains(full.out, "ui request queue unavailable", "an unavailable UI queue is reported");
  ConsoleHost::ui().queueAvailable = true;

  checkContains(runDirect("gps raw").out, "usage: gps raw on | off", "gps raw needs an argument");
  checkContains(runDirect("gps send").out, "usage: gps send", "gps send needs a body");
}

// Test 6. Time, power and performance reporting.
void testTimePowerPerf(void) {
  std::cerr << "test: time, power and perf report their counters\n";
  ConsoleHost::resetDoubles();

  ConsoleHost::time().status = {
      true, true, false, 1788400000000000ULL, 250, Furble::TimeSource::GPS, 4};
  const Result time = runDirect("time status");
  check(time.rc == 0, "time status returns success");
  checkContains(time.out, "valid: true", "time status reports validity");
  checkContains(time.out, "epoch_us: 1788400000000000", "time status reports the epoch");
  checkContains(time.out, "uncertainty_ms: 250", "time status reports the uncertainty");
  checkContains(time.out, "source: gps", "time status names the source");
  checkContains(time.out, "rtc: true", "time status reports the RTC presence");
  checkContains(time.out, "rtc_battery_backed: false", "time status reports the RTC backing");
  checkContains(time.out, "nvs_writes: 4", "time status reports the NVS write count");

  // Every clock source has to come back as a name.
  const std::vector<std::pair<Furble::TimeSource, std::string>> sources = {
      {Furble::TimeSource::NONE,      "none"     },
      {Furble::TimeSource::NVS,       "nvs"      },
      {Furble::TimeSource::RTC,       "rtc"      },
      {Furble::TimeSource::COMPANION, "companion"},
      {Furble::TimeSource::GPS,       "gps"      },
      {Furble::TimeSource::NTP,       "ntp"      },
  };
  for (const auto &source : sources) {
    ConsoleHost::time().status.source = source.first;
    checkContains(runDirect("time status").out, "source: " + source.second,
                  "time source reports as " + source.second);
  }
  ConsoleHost::time().status.source = Furble::TimeSource::GPS;

  checkContains(runDirect("time flush").out, "time: flushed", "time flush confirms the write");
  check(ConsoleHost::time().flushes == 1, "time flush reached the keeper");
  const Result badFlush = runDirect("time flush now");
  check(badFlush.rc != 0, "time flush rejects extra arguments");

  // Bare 'time' defaults to the status report.
  checkContains(runDirect("time").out, "valid: true", "bare time prints the status");

  const Result power = runDirect("power stats");
  check(power.rc == 0, "power stats returns success");
  checkContains(power.out, "lock.no_light_sleep.held:", "power stats reports the sleep lock");
  checkContains(power.out, "lock.cpu_freq_max.held:", "power stats reports the CPU lock");
  checkContains(power.out, "lock.apb_freq_max.held:", "power stats reports the APB lock");
  checkContains(power.out, "pm.locks: host double", "power stats dumps the platform locks");
  check(Furble::Platform::getInstance().dumpPMLocksCount() >= 1,
        "power stats reached the platform lock dump");

  // A real acquire shows up in the counters, so the report is not a constant.
  Furble::Power::getInstance().acquire(Furble::Power::LockType::CPU_FREQ_MAX, "console-test");
  const Result held = runDirect("power stats");
  checkContains(held.out, "lock.cpu_freq_max.held: 1", "an acquired lock is reported as held");
  checkContains(held.out, "lock.cpu_freq_max.owner.console-test: 1", "the lock owner is named");
  Furble::Power::getInstance().release(Furble::Power::LockType::CPU_FREQ_MAX, "console-test");

  // The battery drain logger. With a current sensor it reports milliamps.
  Furble::Platform::getInstance().setBatteryCaps({true, true, true, false});
  const Result log = runDirect("power log 1");
  check(log.rc == 0, "power log returns success");
  checkContains(log.out, "powerlog: timestamp_s,voltage_mv,level_pct,current_ma",
                "the power log prints its header");
  checkContains(log.out, "drain_ma", "the current sensor header names milliamps");
  checkContains(log.out, "powerlog: interval_s: 1", "the power log reports its interval");

  // The console task ticks the logger, so a line lands without further input.
  const long tickStart = ConsoleHost::captureOffset();
  const bool logged = waitFor(
      [tickStart] { return contains(ConsoleHost::capturedSince(tickStart), "powerlog: "); }, 4000);
  check(logged, "the console task emits a power log line on its own");
  checkContains(runDirect("power log off").out, "powerlog: off", "power log off stops the logger");

  // Without a current sensor the header switches to a percent per hour drain,
  // and the runtime estimate comes from the level slope instead.
  Furble::Platform::getInstance().setBatteryCaps({true, true, false, false});
  checkContains(runDirect("power log 1").out, "drain_pct_per_h",
                "a board without a current sensor logs percent per hour");
  Furble::Platform::getInstance().setBatterySample({
      {40, 3700, 0, false},
      40.0f, 3700.0f, 0.0f, 40
  });
  const long slopeStart = ConsoleHost::captureOffset();
  const bool slopeLogged = waitFor(
      [slopeStart] { return contains(ConsoleHost::capturedSince(slopeStart), "powerlog: "); },
      4000);
  check(slopeLogged, "the level slope logger emits a line without a current sensor");
  runDirect("power log off");
  Furble::Platform::getInstance().setBatteryCaps({true, true, true, true});
  Furble::Platform::getInstance().setBatterySample({
      {72, 3900, -110, false},
      72.0f, 3900.0f, -110.0f, 72
  });

  checkContains(runDirect("power log 0").out, "expected an interval from 1 to 86400 seconds or off",
                "a zero interval is rejected");
  checkContains(runDirect("power log later").out, "expected an interval from 1 to 86400",
                "a non-numeric interval is rejected");
  checkContains(runDirect("power log").out, "usage: power log <seconds> | off",
                "power log needs an argument");
  checkContains(runDirect("power stats extra").out, "usage: power stats",
                "power stats rejects extra arguments");

  const Result heap = runDirect("perf heap");
  check(heap.rc == 0, "perf heap returns success");
  checkContains(heap.out, "heap.internal.free:", "perf heap reports the internal heap");
  checkContains(heap.out, "heap.dma.largest_block:", "perf heap reports the DMA heap");
  checkContains(heap.out, "heap.spiram.minimum_free:", "perf heap reports PSRAM");

  const Result tasks = runDirect("perf tasks");
  check(tasks.rc == 0, "perf tasks returns success");
  checkContains(tasks.out, "perf.tasks.window_ms: 1000", "perf tasks reports its window");
  checkContains(tasks.out, "perf.tasks.count:", "perf tasks reports the task count");
  checkContains(tasks.out, "task.console.priority:", "perf tasks reports the console task");
  checkContains(tasks.out, "task.console.cpu_pct:", "perf tasks reports per task CPU");
  checkContains(tasks.out,
                "task.console.stack_high_watermark:", "perf tasks reports the stack headroom");

  // More tasks than the snapshot holds must be reported, not silently counted
  // as zero. This is the guard on the fixed overflow.
  ConsoleHost::misc().syntheticTasks = 40;
  const Result overflow = runDirect("perf tasks");
  check(overflow.rc != 0, "an overflowing task snapshot fails");
  checkContains(overflow.out, "more than 24 tasks", "the snapshot overflow is reported");
  ConsoleHost::misc().syntheticTasks = 0;

  checkContains(runDirect("perf heap extra").out, "usage: perf heap",
                "perf heap rejects extra arguments");
  checkContains(runDirect("perf tasks extra").out, "usage: perf tasks",
                "perf tasks rejects extra arguments");

  // The LVGL statistics and overlay are queued for the UI task.
  ConsoleHost::ui().requests.clear();
  runDirect("perf lvgl");
  check(!ConsoleHost::ui().requests.empty()
            && ConsoleHost::ui().requests.back().request == Furble::UI::Request::PERF
            && ConsoleHost::ui().requests.back().arg == -1,
        "perf lvgl asks the UI task to print its statistics");
  checkContains(runDirect("perf lvgl overlay on").out, "queued: perf lvgl overlay on",
                "perf lvgl overlay on is queued");
  check(ConsoleHost::ui().requests.back().arg == 1, "the overlay request carries the on argument");
  checkContains(runDirect("perf lvgl overlay maybe").out, "usage: perf lvgl overlay on | off",
                "a bad overlay argument prints usage");
  checkContains(runDirect("perf lvgl nonsense arg").out, "usage: perf lvgl",
                "an unknown lvgl argument prints usage");
}

// Test 7. The bluetooth debug tree, asserted at the boundary double.
void testBt(void) {
  std::cerr << "test: the bt command tree parses and reaches the debug backend\n";
  ConsoleHost::resetDoubles();
  auto &bt = ConsoleHost::btDebug();

  runDirect("bt scan");
  check(bt.startScanCalls == 1 && bt.lastScanSeconds == 10 && !bt.lastScanDuplicates,
        "bare bt scan runs a ten second de-duplicated scan");
  runDirect("bt scan 30");
  check(bt.lastScanSeconds == 30, "bt scan takes a duration");
  runDirect("bt scan all 5");
  check(bt.lastScanSeconds == 5 && bt.lastScanDuplicates,
        "bt scan all keeps duplicate advertisements");
  runDirect("bt scan stop");
  check(bt.stopScanCalls == 1, "bt scan stop reaches the backend");
  checkContains(runDirect("bt scan 99999").out, "expected 1-3600 seconds",
                "an out of range scan duration is rejected");
  bt.scanResult = false;
  checkContains(runDirect("bt scan").out, "bt scan unavailable", "a refused scan is reported");
  bt.scanResult = true;
  bt.stopScanResult = false;
  checkContains(runDirect("bt scan stop").out, "no bt scan active",
                "stopping an idle scan is reported");
  bt.stopScanResult = true;

  runDirect("bt explore aa:bb:cc:dd:ee:ff");
  check(bt.startExploreCalls == 1 && bt.lastExploreAddress == "aa:bb:cc:dd:ee:ff"
            && bt.lastPairMode == Furble::BtDebug::PairMode::NONE && !bt.lastExploreKeep,
        "bt explore passes the address through with no pairing");
  runDirect("bt explore aa:bb:cc:dd:ee:ff pair just-works keep");
  check(bt.lastPairMode == Furble::BtDebug::PairMode::JUST_WORKS && bt.lastExploreKeep,
        "bt explore parses the pair mode and the keep flag");
  runDirect("bt explore aa:bb:cc:dd:ee:ff numeric-display");
  check(bt.lastPairMode == Furble::BtDebug::PairMode::NUMERIC_DISPLAY,
        "a bare pair mode is accepted");
  checkContains(runDirect("bt explore not-an-address").out, "usage: bt explore",
                "a malformed address prints usage");
  checkContains(runDirect("bt explore aa:bb:cc:dd:ee:ff keep keep").out, "duplicate keep",
                "a duplicated keep flag is rejected");
  checkContains(runDirect("bt explore aa:bb:cc:dd:ee:ff pair nonsense").out, "expected pair none",
                "an unknown pair mode is rejected");
  runDirect("bt explore read");
  check(bt.readExploreCalls == 1, "bt explore read reaches the backend");
  runDirect("bt explore stop keep");
  check(bt.stopExploreCalls == 1 && bt.lastStopExploreKeep,
        "bt explore stop keep preserves the bond");
  checkContains(runDirect("bt explore stop nonsense").out, "usage: bt explore stop [keep]",
                "an unknown stop argument prints usage");

  runDirect("bt pair yes");
  check(bt.pairConfirmCalls == 1 && bt.lastPairAccept, "bt pair yes confirms");
  runDirect("bt pair no");
  check(bt.pairConfirmCalls == 2 && !bt.lastPairAccept, "bt pair no rejects");
  runDirect("bt pair key 123456");
  check(bt.pairKeyCalls == 1 && bt.lastPairKey == 123456, "bt pair key passes the passkey");
  checkContains(runDirect("bt pair key 12345").out, "usage: bt pair key <6 digits>",
                "a short passkey is rejected");
  checkContains(runDirect("bt pair key abcdef").out, "usage: bt pair key <6 digits>",
                "a non-numeric passkey is rejected");
  bt.pairResult = false;
  checkContains(runDirect("bt pair yes").out, "no pairing confirmation pending",
                "confirming with nothing pending is reported");
  bt.pairResult = true;

  // The GATT journal is real: it is the shared BtDebugJournal the firmware uses.
  checkContains(runDirect("bt journal on").out, "journal: on", "the journal turns on");
  checkContains(runDirect("bt journal dump").out, "", "the journal dumps");
  const Result dumpN = runDirect("bt journal dump 5");
  check(dumpN.rc == 0, "a bounded journal dump returns success");
  checkContains(runDirect("bt journal dump x").out, "usage: bt journal dump [n]",
                "a non-numeric dump count is rejected");
  checkContains(runDirect("bt journal clear").out, "journal: clear", "the journal clears");
  checkContains(runDirect("bt journal off").out, "journal: off", "the journal turns off");
  checkContains(runDirect("bt journal nonsense").out, "usage: bt journal on|off|dump [n]|clear",
                "an unknown journal subcommand prints usage");
}

// Test 8. The command boundaries that reach a subsystem: infrared, feedback,
// the saved camera list, scanning, connect and the restart.
void testBoundaryCommands(void) {
  std::cerr << "test: the hardware-bound commands reach their boundary\n";
  ConsoleHost::resetDoubles();

  // Infrared is gated twice: on the board having an emitter, and on the
  // setting. Both refusals must be distinguishable by a host script.
  ConsoleHost::ir().supported = false;
  const Result noEmitter = runDirect("ir fire");
  check(noEmitter.rc != 0, "ir fire fails without an emitter");
  checkContains(noEmitter.out, "no ir emitter on this board", "the missing emitter is named");
  ConsoleHost::ir().supported = true;

  runDirect("settings set ir off");
  const Result disabled = runDirect("ir fire");
  check(disabled.rc != 0, "ir fire fails while infrared is disabled");
  checkContains(disabled.out, "ir is disabled", "the disabled setting is named");

  runDirect("settings set ir on");
  checkContains(runDirect("ir fire").out, "queued: ir fire", "ir fire reports the queued pulse");
  check(ConsoleHost::ir().fires == 1 && !ConsoleHost::ir().lastHadProtocol,
        "ir fire with no protocol uses the configured one");
  runDirect("ir fire 2");
  check(ConsoleHost::ir().lastHadProtocol
            && ConsoleHost::ir().lastProtocol == Furble::IR::protocol_t::CANON,
        "ir fire takes an explicit protocol");
  checkContains(runDirect("ir fire 9").out, "expected a protocol from 0-3",
                "an out of range protocol is rejected");
  runDirect("settings set ir off");

  // Feedback patterns are queued for the UI task, one request per event name.
  ConsoleHost::ui().requests.clear();
  const char *events[] = {"shutter", "countdown", "connect", "disconnect", "battery"};
  for (const char *event : events) {
    const Result feedback = runDirect(std::string("feedback test ") + event);
    check(feedback.rc == 0, std::string("feedback test ") + event + " returns success");
  }
  check(ConsoleHost::ui().requests.size() == 5, "every feedback event queued a UI request");
  for (const auto &request : ConsoleHost::ui().requests) {
    check(request.request == Furble::UI::Request::FEEDBACK_TEST,
          "the feedback request is the test request");
  }
  checkContains(runDirect("feedback test nonsense").out, "expected shutter, countdown",
                "an unknown feedback event is rejected");

  // The saved camera list, the scan and the connect requests all belong to the
  // UI task, so the console queues them.
  ConsoleHost::ui().requests.clear();
  runDirect("cameras list");
  check(!ConsoleHost::ui().requests.empty()
            && ConsoleHost::ui().requests.back().request == Furble::UI::Request::CAMERAS
            && ConsoleHost::ui().requests.back().arg == 1,
        "cameras list asks the UI task to reload and print the saved list");

  Furble::Scan::getInstance().setActive(true);
  const Result scanList = runDirect("scan list");
  checkContains(scanList.out, "active: true", "scan list reports a running scan");
  Furble::Scan::getInstance().setActive(false);
  checkContains(runDirect("scan list").out, "active: false", "scan list reports an idle scan");

  ConsoleHost::ui().requests.clear();
  checkContains(runDirect("scan start").out, "queued: scan start", "scan start is queued");
  check(ConsoleHost::ui().requests.back().request == Furble::UI::Request::SCAN
            && ConsoleHost::ui().requests.back().arg == 1,
        "scan start carries the start argument");
  checkContains(runDirect("scan stop").out, "queued: scan stop", "scan stop is queued");
  check(ConsoleHost::ui().requests.back().arg == 0, "scan stop carries the stop argument");

  ConsoleHost::ui().requests.clear();
  checkContains(runDirect("connect").out, "queued: connect",
                "a bare connect uses the multi-connect selection");
  check(ConsoleHost::ui().requests.back().request == Furble::UI::Request::CONNECT
            && ConsoleHost::ui().requests.back().arg == -1,
        "a bare connect queues the negative index");
  runDirect("connect 2");
  check(ConsoleHost::ui().requests.back().arg == 2, "connect takes a saved camera index");
  checkContains(runDirect("connect -1").out, "expected a camera index",
                "a negative index is rejected");
  checkContains(runDirect("connect abc").out, "expected a camera index",
                "a non-numeric index is rejected");

  checkContains(runDirect("disconnect").out, "queued: disconnect", "disconnect is queued");
  check(ConsoleHost::ui().requests.back().request == Furble::UI::Request::DISCONNECT,
        "disconnect queues the disconnect request");

  // The audit runs on the UI task, which owns the widget tree.
  ConsoleHost::ui().requests.clear();
  check(runDirect("ui audit").rc == 0, "ui audit returns success");
  check(!ConsoleHost::ui().requests.empty()
            && ConsoleHost::ui().requests.back().request == Furble::UI::Request::AUDIT,
        "ui audit is queued for the UI task");

  // The inertial sensor probe.
  M5.Imu.setEnabled(false);
  M5.Imu.setType(m5::imu_none);
  const Result imuOff = runDirect("imu status");
  check(imuOff.rc == 0, "imu status returns success with no sensor");
  checkContains(imuOff.out, "type: none", "imu status names a missing sensor");
  checkContains(imuOff.out, "enabled: false", "imu status reports the sensor as off");
  checkContains(imuOff.out, "accel: false", "imu status skips the sample when off");

  // Every sensor part number has to come back as a name.
  M5.Imu.setEnabled(true);
  const std::vector<std::pair<m5::imu_t, std::string>> imus = {
      {m5::imu_none,    "none"   },
      {m5::imu_unknown, "unknown"},
      {m5::imu_sh200q,  "sh200q" },
      {m5::imu_mpu6050, "mpu6050"},
      {m5::imu_mpu6886, "mpu6886"},
      {m5::imu_mpu9250, "mpu9250"},
      {m5::imu_bmi270,  "bmi270" },
  };
  for (const auto &imu : imus) {
    M5.Imu.setType(imu.first);
    checkContains(runDirect("imu status").out, "type: " + imu.second,
                  "the inertial sensor reports as " + imu.second);
  }

  M5.Imu.setType(m5::imu_bmi270);
  M5.Imu.setAccel(0.1f, 0.2f, 0.9f);
  M5.Imu.setGyro(1.5f, -2.5f, 0.0f);
  const Result imuOn = runDirect("imu status");
  checkContains(imuOn.out, "type: bmi270", "imu status names the sensor");
  checkContains(imuOn.out, "accel: true 0.100 0.200 0.900", "imu status reports the accelerometer");
  checkContains(imuOn.out, "gyro: true 1.500 -2.500 0.000", "imu status reports the gyroscope");
  M5.Imu.setType(m5::imu_mpu6886);

  // The restart is a real code path, the platform double just counts it.
  const uint32_t restarts = Furble::Platform::getInstance().restartCount();
  checkContains(runDirect("reboot").out, "reboot: now", "reboot announces itself");
  check(Furble::Platform::getInstance().restartCount() == restarts + 1,
        "reboot reached the platform restart");
}

// Test 9. The flash preparation state machine, which disarms the external PMIC
// watchdog so a serial upload cannot be interrupted.
void testFlashStateMachine(void) {
  std::cerr << "test: flash prepare and cancel drive the PMIC watchdog\n";
  ConsoleHost::resetDoubles();
  auto &platform = Furble::Platform::getInstance();

  platform.setFlashPrepareShouldFail(false);
  platform.setFlashCancelShouldFail(false);
  runDirect("flash cancel");

  const Result prepare = runDirect("flash prepare");
  check(prepare.rc == 0, "flash prepare returns success");
  checkContains(prepare.out, "flash.ready: true", "flash prepare reports readiness");
  checkContains(prepare.out, "flash.watchdog: disabled", "flash prepare disarms the watchdog");
  checkContains(prepare.out, "flash.download_recovery: unlocked", "the download lock is released");
  check(platform.isFlashReady(), "the PMIC is left disarmed");

  const Result cancel = runDirect("flash cancel");
  check(cancel.rc == 0, "flash cancel returns success");
  checkContains(cancel.out, "flash.ready: false", "flash cancel reports the state change");
  checkContains(cancel.out, "flash.watchdog: armed", "flash cancel re-arms the watchdog");
  check(!platform.isFlashReady(), "the PMIC is left armed");

  // A PMIC that refuses must be reported, never silently treated as prepared.
  platform.setFlashPrepareShouldFail(true);
  const Result refused = runDirect("flash prepare");
  check(refused.rc != 0, "a refused preparation fails");
  checkContains(refused.out, "PMIC flash preparation failed",
                "a refused preparation names the physical recovery path");
  check(!platform.isFlashReady(), "a refused preparation leaves the watchdog armed");
  platform.setFlashPrepareShouldFail(false);

  runDirect("flash prepare");
  platform.setFlashCancelShouldFail(true);
  const Result refusedCancel = runDirect("flash cancel");
  check(refusedCancel.rc != 0, "a refused restore fails");
  checkContains(refusedCancel.out, "PMIC watchdog restore failed",
                "a refused restore warns to keep the device powered");
  platform.setFlashCancelShouldFail(false);
  runDirect("flash cancel");
}

// Test 10. Log level control, the one command that reconfigures the firmware's
// own diagnostics.
void testLogLevels(void) {
  std::cerr << "test: log sets the level for a tag\n";
  ConsoleHost::resetDoubles();

  const Result set = runDirect("log furble verbose");
  check(set.rc == 0, "log returns success");
  checkContains(set.out, "log: furble verbose", "log echoes the tag and level");
  check(ConsoleHost::misc().lastLogTag == "furble", "the tag reached the log subsystem");
  check(ConsoleHost::misc().lastLogLevel == ESP_LOG_VERBOSE, "the level reached the log subsystem");

  // A prefix is enough, so 'v' means verbose and 'w' means warn.
  runDirect("log * w");
  check(ConsoleHost::misc().lastLogTag == "*" && ConsoleHost::misc().lastLogLevel == ESP_LOG_WARN,
        "a level prefix and the wildcard tag both work");
  runDirect("log furble none");
  check(ConsoleHost::misc().lastLogLevel == ESP_LOG_NONE, "none silences a tag");

  const Result unknown = runDirect("log furble shouty");
  check(unknown.rc != 0, "an unknown level fails");
  checkContains(unknown.out, "expected none, error, warn, info, debug or verbose",
                "the level error names the levels");
  const Result missing = runDirect("log furble");
  check(missing.rc != 0, "log without a level fails");
  checkContains(missing.out, "usage: log <tag>", "log prints its usage");
}

// Test 11. Provisioning: a real TLV blob decoded and applied through the
// production decoder and the real Settings store.
void testProvision(void) {
  std::cerr << "test: provision decodes and applies a real TLV blob\n";
  ConsoleHost::resetDoubles();

  // brightness is wire id 1, a u8. wifi_ssid is parsed but deliberately not
  // applied until the WiFi backend lands, so it must be reported as deferred.
  Furble::ProvisionTLV::ProvisionBundle bundle;
  Furble::ProvisionTLV::SettingValue brightness;
  brightness.wireId = 1;
  brightness.type = Furble::ProvisionTLV::ValueType::U8;
  brightness.value = {99};
  bundle.settings.push_back(brightness);
  const Furble::ProvisionTLV::ByteString text {'f', 'u', 'r', 'b', 'l', 'e'};
  bundle.wifiSsid = text;
  bundle.wifiPsk = text;
  bundle.companionPassword = text;
  bundle.mqttUri = text;
  bundle.mqttUsername = text;
  bundle.mqttPassword = text;
  bundle.mqttBaseTopic = text;

  std::vector<uint8_t> encoded;
  if (!check(Furble::ProvisionTLV::encode(bundle, encoded), "the test blob encodes")) {
    return;
  }

  static const char *HEX = "0123456789abcdef";
  std::string hex;
  for (const uint8_t byte : encoded) {
    hex.push_back(HEX[byte >> 4]);
    hex.push_back(HEX[byte & 0x0f]);
  }

  const Result applied = runDirect("provision " + hex);
  check(applied.rc == 0, "a valid blob returns success");
  checkContains(applied.out,
                "provision: decoded " + std::to_string(encoded.size()) + " bytes as hex",
                "the blob is reported as hex with its length");
  for (const char *field : {"wifi_ssid", "wifi_psk", "companion_password", "mqtt_uri",
                            "mqtt_username", "mqtt_password", "mqtt_base_topic"}) {
    checkContains(applied.out, std::string("provision: ") + field + " parsed (not applied",
                  std::string(field) + " is named as deferred");
  }
  // A deferred field carries a secret, so its value must never be printed.
  check(!contains(applied.out, "furble\n"), "no deferred field value is printed");
  checkContains(applied.out, "provision: setting 1 (brightness) applied",
                "an applied setting names its key");
  checkContains(applied.out, "1 setting(s) applied, 7 field(s) deferred",
                "the summary counts applied and deferred fields");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 99,
        "the provisioned value reached the real Settings store");

  // The gesture settings apply immediately, so a provision write must start the
  // 50 Hz timer rather than waiting for the next boot. reloadProvisionSetting()
  // is the only writer that reaches the UI here: deleting its IMU cases makes
  // this fail.
  {
    Furble::ProvisionTLV::ProvisionBundle gestures;
    Furble::ProvisionTLV::SettingValue wake;
    wake.wireId = 72;
    wake.type = Furble::ProvisionTLV::ValueType::U8;
    wake.value = {3};
    gestures.settings.push_back(wake);
    Furble::ProvisionTLV::SettingValue trigger;
    trigger.wireId = 73;
    trigger.type = Furble::ProvisionTLV::ValueType::BOOL;
    trigger.value = {1};
    gestures.settings.push_back(trigger);

    std::vector<uint8_t> gestureBytes;
    if (check(Furble::ProvisionTLV::encode(gestures, gestureBytes), "the gesture blob encodes")) {
      std::string gestureHex;
      for (const uint8_t byte : gestureBytes) {
        gestureHex.push_back(HEX[byte >> 4]);
        gestureHex.push_back(HEX[byte & 0x0f]);
      }
      const unsigned before = ConsoleHost::ui().gestureNotifications;
      const Result gestureRun = runDirect("provision " + gestureHex);
      check(gestureRun.rc == 0, "the gesture blob applies");
      check(Furble::Settings::load<uint8_t>(Furble::Settings::IMU_WAKE) == 3,
            "a provisioned imu_wake reaches the real Settings store");
      check(Furble::Settings::load<bool>(Furble::Settings::IMU_TRIG),
            "a provisioned imu_trigger reaches the real Settings store");
      check(ConsoleHost::ui().gestureNotifications >= before + 2,
            "a provisioned gesture setting notifies the UI task");
    }
  }

  // An empty bundle is valid and reports that it carried nothing.
  Furble::ProvisionTLV::ProvisionBundle empty;
  std::vector<uint8_t> emptyBytes;
  if (Furble::ProvisionTLV::encode(empty, emptyBytes)) {
    std::string emptyHex;
    for (const uint8_t byte : emptyBytes) {
      emptyHex.push_back(HEX[byte >> 4]);
      emptyHex.push_back(HEX[byte & 0x0f]);
    }
    if (!emptyHex.empty()) {
      checkContains(runDirect("provision " + emptyHex).out, "provision: no fields",
                    "an empty bundle reports no fields");
    }
  }

  const Result badText = runDirect("provision nothex!!");
  check(badText.rc != 0, "an undecodable blob fails");
  checkContains(badText.out, "error: provision:", "the text decode error is reported");

  const Result truncated = runDirect("provision " + hex.substr(0, hex.size() - 6));
  check(truncated.rc != 0, "a truncated blob fails");
  checkContains(truncated.out, "error: provision:", "the byte decode error is reported");

  const Result noArg = runDirect("provision");
  check(noArg.rc != 0, "provision without a blob fails");
  checkContains(noArg.out, "usage: provision <hex|base64 TLV blob>", "provision prints its usage");

  // Restore a sane brightness for any later assertion.
  runDirect("settings set brightness 128");
}

// Test 12. Error handling at the dispatcher: an unknown command, an empty line,
// and a handler that reports failure.
void testErrorPaths(void) {
  std::cerr << "test: unknown commands and bad arguments fail loudly\n";
  ConsoleHost::resetDoubles();

  int rc = 0;
  check(esp_console_run("definitely_not_a_command", &rc) == ESP_ERR_NOT_FOUND,
        "an unknown command is not found");
  check(esp_console_run("   ", &rc) == ESP_ERR_INVALID_ARG, "an empty line is rejected");

  // The console task turns the not-found error into a message for the operator.
  checkContains(ConsoleHost::runLine("definitely_not_a_command"),
                "error: unknown command, try 'help'", "the task reports an unknown command");

  // Every usage error returns non-zero, which is what a host script branches on.
  const std::vector<std::string> failures = {
      "settings", "shutter", "focus",     "scan",  "gps raw", "power",    "perf",
      "bt",       "log",     "provision", "debug", "imu",     "feedback", "ui",
  };
  for (const auto &line : failures) {
    const Result result = runDirect(line);
    check(result.err == ESP_OK, "'" + line + "' dispatches to its handler");
    check(result.rc != 0, "'" + line + "' returns a non-zero result");
    check(contains(result.out, "error: "), "'" + line + "' prints an error line");
  }
}

// Test 13. The transport and the line editor, driven by typing at the real
// console task rather than calling the dispatcher.
void testConsoleTaskTransport(void) {
  std::cerr << "test: the console task echoes, edits and dispatches typed lines\n";
  ConsoleHost::resetDoubles();

  check(ConsoleHost::misc().usbDriverInstalls == 1,
        "the console installed the USB-Serial/JTAG driver once");
  check(ConsoleHost::misc().vfsUseDriverCalls == 1,
        "the console moved its output onto that driver");

  checkContains(ConsoleHost::runLine("version"), "version: host",
                "a typed line reaches the command handler");

  // The line editor: a backspace removes the last character, so the mistyped
  // 'versionx' still dispatches as 'version'.
  const long start = ConsoleHost::captureOffset();
  ConsoleHost::feedBytes("versionx\bing\b\b\b\n");
  const bool dispatched = waitFor(
      [start] { return contains(ConsoleHost::capturedSince(start), "version: host"); }, 5000);
  check(dispatched, "a backspace edited line dispatches the corrected command");

  // A blank line just reprints the prompt, it does not dispatch.
  const std::string blank = ConsoleHost::runLine("");
  check(blank.empty(), "an empty line prints nothing but the prompt");

  // Overlong input is truncated at the line limit instead of overflowing.
  const long overStart = ConsoleHost::captureOffset();
  ConsoleHost::feedBytes(std::string(200, 'z') + "\n");
  const bool rejected = waitFor(
      [overStart] {
        return contains(ConsoleHost::capturedSince(overStart), "error: unknown command");
      },
      5000);
  check(rejected, "an overlong line is truncated and rejected, not overflowed");
}

// Test 14. The debug dump family against a live camera through the real
// Control state machine and the mock BLE stack.
void testDebugWithLiveCamera(void) {
  std::cerr << "test: the debug dumps report a live camera through real Control\n";
  ConsoleHost::resetDoubles();

  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  static bool controlStarted = false;
  if (!controlStarted) {
    controlStarted = true;
    // Through the shim, exactly as main() starts it on device: the shim owns
    // the thread and furbleHostStopTasks() joins it before this process exits.
    xTaskCreate(control_task, "control", 8192, &Furble::Control::getInstance(), 4, nullptr);
  }

  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  auto &control = Furble::Control::getInstance();
  control.addActive(camera);
  control.connectAll(false);
  const bool active =
      waitFor([&control] { return control.getState() == Furble::Control::STATE_ACTIVE; }, 8000);
  if (!check(active, "the virtual camera reaches the active state")) {
    control.disconnect();
    NimBLEDevice::resetMock();
    return;
  }

  const Result status = runDirect("status");
  checkContains(status.out, "state: active", "status reports the active state");
  checkContains(status.out, "targets: 1", "status counts the connected target");

  const Result cameras = runDirect("cameras status");
  check(cameras.rc == 0, "cameras status returns success");
  checkContains(cameras.out, "targets: 1", "cameras status counts the targets");
  checkContains(cameras.out, "target0.name: ", "cameras status names the target");
  checkContains(cameras.out, "target0.connected: true", "cameras status reports the link");

  const Result debugControl = runDirect("debug control");
  check(debugControl.rc == 0, "debug control returns success");
  checkContains(debugControl.out, "control.state: active", "the control dump reports the state");
  checkContains(debugControl.out, "control.targets: 1", "the control dump counts targets");
  checkContains(debugControl.out, "control.connected: 1", "the control dump counts live links");
  checkContains(debugControl.out, "control.zombies: 0", "the control dump counts drained targets");
  checkContains(debugControl.out,
                "control.sleep_lock_held: ", "the control dump reports the sleep lock");
  checkContains(debugControl.out,
                "control.power_level: ", "the control dump reports the transmit power");

  const Result debugCamera = runDirect("debug camera");
  check(debugCamera.rc == 0, "debug camera returns success");
  checkContains(debugCamera.out, "cameras.targets: 1", "the camera dump counts targets");
  checkContains(debugCamera.out, "camera0.type: fujifilm-basic", "the camera dump names the type");
  checkContains(debugCamera.out, "camera0.connected: true", "the camera dump reports the link");
  checkContains(debugCamera.out, "camera0.address: ", "the camera dump prints the address");
  checkContains(debugCamera.out,
                "camera0.profile: ", "the camera dump names the connection profile");

  checkContains(runDirect("debug camera 0").out,
                "camera0.name: ", "the camera dump takes a target index");
  const Result badIndex = runDirect("debug camera 9");
  check(badIndex.rc != 0, "an out of range target index fails");
  checkContains(badIndex.out, "expected a target index", "the index error is named");

  const Result debugBle = runDirect("debug ble");
  check(debugBle.rc == 0, "debug ble returns success");
  checkContains(debugBle.out, "ble.initialized: true", "the BLE dump reports the stack state");
  checkContains(debugBle.out, "ble.address: ", "the BLE dump prints the controller address");
  checkContains(debugBle.out, "ble.clients_created: ", "the BLE dump reports the client pool");
  checkContains(debugBle.out, "ble.clients_max: 3", "the BLE dump reports the pool ceiling");
  checkContains(debugBle.out, "ble.target0: ", "the BLE dump cross-references each target");
  checkContains(debugBle.out, "client=true", "the live target resolves to a NimBLE client");

  // The camera control commands only work in the active state, which is why
  // they report the state when they refuse.
  checkContains(runDirect("shutter press").out, "sent: ok", "shutter press reaches the target");
  checkContains(runDirect("shutter release").out, "sent: ok", "shutter release reaches the target");
  checkContains(runDirect("focus press").out, "sent: ok", "focus press reaches the target");
  checkContains(runDirect("focus release").out, "sent: ok", "focus release reaches the target");
  const Result hold = runDirect("shutter hold 20");
  check(hold.rc == 0, "shutter hold returns success");
  checkContains(hold.out, "sent: ok", "shutter hold pairs a press with a release");
  checkContains(runDirect("shutter hold 90000").out, "expected 0-60000 ms",
                "an out of range hold is rejected");

  // 'debug all' is the single paste a bug report carries.
  const Result all = runDirect("debug all");
  check(all.rc == 0, "debug all returns success");
  for (const char *section : {"== status ==", "== control ==", "== cameras ==", "== ble ==",
                              "== power ==", "== heap ==", "== gps ==", "== settings =="}) {
    checkContains(all.out, section, std::string("debug all includes ") + section);
  }

  // The aliases share their implementations with the perf and gps commands.
  checkContains(runDirect("debug heap").out, "heap.internal.free:", "debug heap aliases perf heap");
  checkContains(runDirect("debug gps").out, "enabled: ", "debug gps aliases the gps status");
  checkContains(runDirect("debug settings").out,
                "brightness: ", "debug settings aliases the settings list");
  checkContains(runDirect("debug power").out,
                "lock.no_light_sleep.held:", "debug power aliases power stats");

  control.disconnect();
  waitFor([&control] { return control.getState() == Furble::Control::STATE_IDLE; }, 5000);

  // With no camera the control commands report the state and refuse.
  const Result idle = runDirect("shutter press");
  check(idle.rc != 0, "shutter press fails with no camera connected");
  checkContains(idle.out, "state: idle", "the refusal reports the control state");
  checkContains(idle.out, "no camera connected", "the refusal names the reason");

  NimBLEDevice::resetMock();
}

// Set by the task the shim must refuse to start once shutdown has begun.
std::atomic<bool> g_LateTaskRan {false};

// Test 15. The host-harness task lifetime contract itself. furbleHostStopTasks()
// copies the task list before it joins, so a task created after that copy is
// never joined and outlives main() exactly as a detached task did. The shim has
// to refuse it. This test runs last because it stops every shim task: the
// console task is joined here, so nothing after it can drive a command.
void testTaskCreationAfterShutdownIsRejected(void) {
  std::cerr << "test: the shim refuses a task created after shutdown has begun\n";

  furbleHostStopTasks();

  g_LateTaskRan = false;
  const BaseType_t created =
      xTaskCreate([](void *) { g_LateTaskRan = true; }, "late", 2048, nullptr, 1, nullptr);
  check(created == pdFAIL, "xTaskCreate is refused once shutdown has begun");

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(!g_LateTaskRan.load(), "the refused task never ran");
}
}  // namespace

int main(void) {
  // Stop and join every shim task before this scope ends, so no firmware task
  // is still running when static destruction frees what it reads. The control
  // task reaps the zombie drain on every 50 ms tick, and the drain still holds
  // the target the last disconnect handed it, so a thread left running past
  // ~Control walks a freed vector.
  FurbleHostTaskScope taskScope;

  // Every command prints to stdout, so the suite reads its assertions back out
  // of a captured stdout. The file lives in the build tree, and carries the
  // process id so two concurrent runs never share one.
  char capturePath[256];
  snprintf(capturePath, sizeof(capturePath), "%s/furble-console-%d.out", FURBLE_CONSOLE_CAPTURE_DIR,
           static_cast<int>(getpid()));
  ConsoleHost::startCapture(capturePath);
  Furble::Settings::init();
  Furble::Console::init();

  // The console task prints its banner as soon as it starts. Wait for it so the
  // first captured offset is past the banner.
  waitFor([] { return ConsoleHost::misc().usbDriverInstalls > 0; }, 2000);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  testCommandTable();
  testSubcommandSets();
  testStatusAndVersion();
  testSettings();
  testGPS();
  testTimePowerPerf();
  testBt();
  testBoundaryCommands();
  testFlashStateMachine();
  testLogLevels();
  testProvision();
  testErrorPaths();
  testConsoleTaskTransport();
  testDebugWithLiveCamera();

  // Last: it stops and joins every shim task.
  testTaskCreationAfterShutdownIsRejected();

  std::cerr << (g_Failures == 0 ? "PASS" : "FAIL") << ": " << (g_Checks - g_Failures) << "/"
            << g_Checks << " checks\n";
  fflush(stdout);
  std::remove(capturePath);
  return (g_Failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
