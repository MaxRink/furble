#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <SDL2/SDL.h>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include <driver/uart.h>

#include "CameraList.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "Scan.h"
#include "ble_sim.h"
#include "capture.h"
#include "clock.h"
#include "driver.h"
#include "fuzz.h"
#include "platform_state.h"
#include "power_profiler.h"

namespace Furble::Sim {
namespace {

enum class StepType {
  WAIT,
  STALL,
  KEY,
  BTN,
  CAPTURE,
  UART_DUMP,
  UART_MODE,
  UART_EVENT,
  GPS_RESTART,
  HOME,
  BACK,
  REPORT,
  RESTART,
  ACTION,
  ASSERT,
  ASSERT_EVENTUALLY,
  ASSERT_EVENTUALLY_VIRTUAL,
  XASSERT,
  ASSERT_MAX,
  ASSERT_MIN,
  PRINT,
  EXIT,
};

struct Step {
  StepType type;
  uint32_t milliseconds = 0;
  SDL_Keycode key = SDLK_UNKNOWN;
  bool hold = false;
  std::string name;
  std::string expected;
  uint32_t timeoutMilliseconds = 0;
  // xassert only: the documented gap is board dependent, so a match is
  // informational rather than a promotion signal. See the xassert parser.
  bool boardVaries = false;
  scenario_action_t action;
};

constexpr uint32_t MAX_EVENTUAL_TIMEOUT_MS = 60000;

std::vector<Step> steps;
std::map<std::string, std::string> scenarioSettings;
std::string captureDirectory = ".pio/furble-sim-captures";
std::string reportDirectory = ".pio/furble-sim-reports";
std::string scenarioName = "interactive";
size_t stepIndex = 0;
uint32_t waitUntil = 0;
uint32_t releaseAt = 0;
SDL_Keycode pressedKey = SDLK_UNKNOWN;
Furble::UI *scenarioUi = nullptr;
bool configured = false;
Furble::UI *backTarget = nullptr;
bool waiting = false;
std::atomic<int> requestedExit {-1};
battery_reading_t simulatedBattery = {80, 4000, 0, false};
bool simulatedPowerOff = false;

// Restart seam (plan 156). The `restart` verb models a device reboot by
// re-executing the simulator binary with the same arguments: every thread,
// singleton, and RAM state is wiped exactly as an esp_restart() wipes them,
// while the NVS-backed preferences file persists exactly as flash does. The
// resumed process skips the steps already executed via FURBLE_SIM_RESTART_STEP
// and skips the fresh-scenario preferences wipe, so scripted state written
// before the restart is what the rebooted app boots from. The step itself only
// requests the orderly shutdown that plan 158 built for `exit`; main() runs the
// re-exec after every task has joined and the panel has closed.
//
// restartPending is its own shutdown request rather than a requestExit(0) call:
// requestExit() is first-wins, so pinning zero here would swallow every failure
// raised between this step and the re-exec (a liveness violation, an action
// error) and reboot anyway. Leaving requestedExit unset lets any of them win,
// and main() only re-execs when exitResult() is still zero.
std::vector<std::string> savedArguments;
bool resumedBoot = false;
std::atomic<bool> restartPending {false};
const char *RESTART_STEP_ENV = "FURBLE_SIM_RESTART_STEP";

// Continuous UI liveness invariant (plan 155). Every driver tick, if the UI
// presents the Connected screen (the same three-way check the ui.connected
// query makes) while fewer camera links are actually up than the session has
// targets, a grace timer starts. Divergence outliving the grace period is a
// liveness violation: the 2026-08-28 hardware incident kept a Connected screen
// up while neither camera had a live link. Scenarios that deliberately
// construct the divergence opt out of enforcement with "seed liveness_check
// false"; detection still counts violations so such a scenario can assert the
// invariant would have fired.
constexpr uint32_t LIVENESS_GRACE_DEFAULT_MS = 3000;
bool livenessArmed = false;
bool livenessLatched = false;
uint32_t livenessDeadline = 0;
uint32_t livenessViolations = 0;

SDL_Keycode keyCode(const std::string &name) {
  if (name == "up") {
    return SDLK_UP;
  }
  if (name == "down") {
    return SDLK_DOWN;
  }
  if (name == "left") {
    return SDLK_LEFT;
  }
  if (name == "right") {
    return SDLK_RIGHT;
  }
  if (name == "return" || name == "enter") {
    return SDLK_RETURN;
  }
  return SDLK_UNKNOWN;
}

bool parseBool(const std::string &value) {
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

uint32_t parseUnsigned(const std::string &value) {
  try {
    if (value.empty() || value[0] == '+' || value[0] == '-') {
      throw std::invalid_argument("signed or empty unsigned value");
    }
    size_t parsed = 0;
    const unsigned long number = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || number > std::numeric_limits<uint32_t>::max()) {
      throw std::out_of_range("uint32 range");
    }
    return static_cast<uint32_t>(number);
  } catch (const std::exception &) {
    std::cerr << "Invalid scenario setting value: " << value << '\n';
    std::exit(2);
  }
}

std::string trim(std::string value) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

void pushKey(SDL_Keycode key, bool pressed) {
  int gpio = -1;
  switch (key) {
    case SDLK_LEFT:
      gpio = 39;
      break;
    case SDLK_DOWN:
      gpio = 38;
      break;
    case SDLK_RIGHT:
      gpio = 37;
      break;
    case SDLK_UP:
      gpio = 36;
      break;
    default:
      break;
  }

  if (gpio >= 0) {
    if (pressed) {
      lgfx::gpio_lo(static_cast<uint32_t>(gpio));
    } else {
      lgfx::gpio_hi(static_cast<uint32_t>(gpio));
    }
  }
}

// Physical button set per simulated board, used to reject at parse time a
// scenario that presses a button the board does not carry. The Sticks expose
// front BtnA, top BtnB and the side BtnPWR; the Cores expose front BtnA/BtnB/
// BtnC. The press itself runs through UI::simPressButton, which maps each
// silk-screen button to the same per-board input device furble wires up in
// initInputDevices, so the UI reacts exactly as it does to a hardware press.
// Headless runs cannot drive input through the SDL panel's emulated GPIOs
// (that path needs a display-backed event pump), so the button is injected on
// the UI task rather than by toggling pins.
#if defined(FURBLE_M5COREX)
// M5Stack Core / Core2: three front buttons A/B/C, no dedicated power button.
constexpr const char *kSimButtons[] = {"a", "b", "c"};
#else
// M5StickC / M5StickC-Plus / M5StickS3: front BtnA, top BtnB, side BtnPWR.
constexpr const char *kSimButtons[] = {"a", "b", "pwr"};
#endif

bool buttonKnown(const std::string &name) {
  for (const char *button : kSimButtons) {
    if (name == button) {
      return true;
    }
  }
  return false;
}

int32_t parseSigned(const std::string &value, const char *name);
uint16_t batteryVoltage(const std::string &value);

uint64_t parseUnsigned64(const std::string &value, const char *name) {
  try {
    if (value.empty() || value[0] == '+' || value[0] == '-') {
      throw std::invalid_argument("negative or empty");
    }
    size_t parsed = 0;
    const uint64_t number = std::stoull(value, &parsed, 10);
    if (parsed != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return number;
  } catch (const std::exception &) {
    std::cerr << "Invalid " << name << ": " << value << '\n';
    std::exit(2);
  }
}

bool booleanSeedValue(const std::string &value) {
  return value == "0" || value == "1" || value == "true" || value == "false" || value == "yes"
         || value == "no" || value == "on" || value == "off";
}

std::vector<std::string> scriptWords(const std::string &line) {
  std::istringstream input(line);
  std::vector<std::string> result;
  std::string word;
  while (input >> word) {
    result.push_back(word);
  }
  return result;
}

void validateSeed(const std::string &name, const std::string &value) {
  if (name == "clock_ms" || name == "liveness_grace_ms") {
    parseUnsigned(value);
    return;
  }

  constexpr const char *byteSeeds[] = {
      "brightness", "inactivity", "display_off", "gps_rate",  "gps_constel",
      "gps_power",  "gps_duty",   "cpu_freq",    "tx_power",  "scan_mode",
      "text_size",  "auto_off",   "low_batt",    "fb_output",
  };
  if (std::find(std::begin(byteSeeds), std::end(byteSeeds), name) != std::end(byteSeeds)) {
    if (parseUnsigned(value) > std::numeric_limits<uint8_t>::max()) {
      std::cerr << "Invalid " << name << ": " << value << '\n';
      std::exit(2);
    }
    return;
  }

  constexpr const char *booleanSeeds[] = {
      "gps",           "gps_nmea",          "fauxny",
      "autoconnect",   "reconnect",         "sleep_conn",
      "boot_splash",   "connect_fail",      "no_touch",
      "saved_camera",  "scan_start_probe",  "ble_saved",
      "recon_backoff", "auto_off_charging", "imu",
      "imu_sensor",    "liveness_check",
  };
  if (std::find(std::begin(booleanSeeds), std::end(booleanSeeds), name) != std::end(booleanSeeds)) {
    if (!booleanSeedValue(value)) {
      std::cerr << "Invalid " << name << ": " << value << '\n';
      std::exit(2);
    }
    return;
  }

  if (name == "watchdog") {
#if defined(FURBLE_M5STICKS3)
    if (!booleanSeedValue(value)) {
      std::cerr << "Invalid watchdog: " << value << '\n';
      std::exit(2);
    }
    return;
#else
    std::cerr << "Unsupported scenario seed on this board: watchdog\n";
    std::exit(2);
#endif
  }

  constexpr const char *intervalSeeds[] = {
      "interval_count", "interval_delay", "interval_shutter", "interval_wait", "bulb_duration",
  };
  if (std::find(std::begin(intervalSeeds), std::end(intervalSeeds), name)
      != std::end(intervalSeeds)) {
    if (parseUnsigned(value) > std::numeric_limits<uint16_t>::max()) {
      std::cerr << "Invalid " << name << ": " << value << '\n';
      std::exit(2);
    }
    return;
  }

  if (name == "battery_level") {
    if (parseUnsigned(value) > 100) {
      std::cerr << "Invalid battery_level: " << value << '\n';
      std::exit(2);
    }
    return;
  } else if (name == "battery_voltage") {
    batteryVoltage(value);
    return;
  } else if (name == "battery_current") {
    parseSigned(value, "battery_current");
    return;
  } else if (name == "battery_charging") {
    if (!booleanSeedValue(value)) {
      std::cerr << "Invalid battery_charging: " << value << '\n';
      std::exit(2);
    }
    return;
  } else if (name == "ble_peers") {
    if (!bleTopologyIsValid(value)) {
      std::cerr << "Invalid ble_peers: " << value << '\n';
      std::exit(2);
    }
    return;
  } else if (name == "scan_timeout") {
    parseUnsigned(value);
    return;
  } else if (name == "gps_uart_mode") {
    if (value != "ack" && value != "nack" && value != "timeout" && value != "malformed"
        && value != "partial" && value != "write-error" && value != "pause") {
      std::cerr << "Invalid gps_uart_mode: " << value << '\n';
      std::exit(2);
    }
    return;
  }

  std::cerr << "Unknown scenario seed: " << name << '\n';
  std::exit(2);
}

void readScript(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "Could not open simulator script: " << path << '\n';
    std::exit(2);
  }

  std::string line;
  while (std::getline(file, line)) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty()) {
      continue;
    }

    const std::vector<std::string> args = scriptWords(trim(line));
    const auto rejectArity = [](const std::string &name, const std::string &usage) {
      std::cerr << name << " requires " << usage << '\n';
      std::exit(2);
    };
    const auto exactArgs = [&args](size_t count) { return args.size() == count; };

    if (command == "seed") {
      if (!exactArgs(3)) {
        rejectArity("seed", "exactly a name and value");
      }
      if (scenarioSettings.find(args[1]) != scenarioSettings.end()) {
        std::cerr << "Duplicate scenario seed: " << args[1] << '\n';
        std::exit(2);
      }
      validateSeed(args[1], args[2]);
      scenarioSettings[args[1]] = args[2];
      continue;
    }

    if (command == "wait" || command == "advance") {
      if (!exactArgs(2)) {
        rejectArity(command, "exactly one duration");
      }
      Step step;
      step.type = StepType::WAIT;
      step.milliseconds = parseUnsigned(args[1]);
      steps.push_back(step);
    } else if (command == "stall") {
      if (!exactArgs(2)) {
        rejectArity("stall", "exactly one non-zero duration");
      }
      Step step;
      step.type = StepType::STALL;
      step.milliseconds = parseUnsigned(args[1]);
      if (step.milliseconds == 0) {
        rejectArity("stall", "exactly one non-zero duration");
      }
      steps.push_back(step);
    } else if (command == "key" || command == "press") {
      if (!exactArgs(2)) {
        rejectArity(command, "exactly one key");
      }
      Step step;
      step.type = StepType::KEY;
      step.key = keyCode(args[1]);
      if (step.key == SDLK_UNKNOWN) {
        std::cerr << "Unknown simulator key: " << args[1] << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "btn" || command == "button") {
      // Press a physical button by name: a, b, c or pwr. An optional second
      // token "hold"/"long" selects the left-button long-press escape. Absent
      // it, the button taps. The name is validated against the board's button
      // set here so pressing an absent button (BtnC on a Stick, BtnPWR on a
      // Core) fails at parse time.
      if (args.size() < 2 || args.size() > 3) {
        rejectArity(command, "a button and optional hold modifier");
      }
      std::string name = args[1];
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (!buttonKnown(name)) {
        std::cerr << "Unknown or unavailable simulator button for this board: " << name << '\n';
        std::exit(2);
      }
      Step step;
      step.type = StepType::BTN;
      step.name = name;
      std::string hold = args.size() == 3 ? args[2] : "";
      if (hold == "hold" || hold == "long") {
        step.hold = true;
      } else if (!hold.empty()) {
        std::cerr << "Unknown simulator button modifier: " << hold << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "capture") {
      if (!exactArgs(2)) {
        rejectArity("capture", "exactly one file name");
      }
      Step step;
      step.type = StepType::CAPTURE;
      step.name = args[1];
      steps.push_back(step);
    } else if (command == "uart-dump") {
      if (!exactArgs(1)) {
        rejectArity("uart-dump", "no arguments");
      }
      Step step;
      step.type = StepType::UART_DUMP;
      steps.push_back(step);
    } else if (command == "uart-mode" || command == "gps-uart") {
      if (!exactArgs(2)) {
        rejectArity(command, "exactly one mode");
      }
      Step step;
      step.type = StepType::UART_MODE;
      step.name = args[1];
      if (step.name != "ack" && step.name != "nack" && step.name != "timeout"
          && step.name != "malformed" && step.name != "partial" && step.name != "write-error"
          && step.name != "pause") {
        std::cerr
            << "uart-mode requires ack, nack, timeout, malformed, partial, write-error or pause\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "uart-event" || command == "gps-uart-event") {
      if (!exactArgs(2)) {
        rejectArity(command, "exactly one event");
      }
      Step step;
      step.type = StepType::UART_EVENT;
      step.name = args[1];
      if (step.name != "data" && step.name != "fifo" && step.name != "buffer"
          && step.name != "break" && step.name != "parity" && step.name != "frame"
          && step.name != "pattern") {
        std::cerr << "uart-event requires data, fifo, buffer, break, parity, frame or pattern\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "gps-restart") {
      if (!exactArgs(2)) {
        rejectArity("gps-restart", "exactly one mode");
      }
      Step step;
      step.type = StepType::GPS_RESTART;
      step.name = args[1];
      if (step.name != "hot" && step.name != "warm" && step.name != "cold") {
        std::cerr << "gps-restart requires hot, warm or cold\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "home") {
      if (!exactArgs(1)) {
        rejectArity("home", "no arguments");
      }
      Step step;
      step.type = StepType::HOME;
      steps.push_back(step);
    } else if (command == "back") {
      if (!exactArgs(1)) {
        rejectArity("back", "no arguments");
      }
      Step step;
      step.type = StepType::BACK;
      steps.push_back(step);
    } else if (command == "report") {
      if (!exactArgs(2)) {
        rejectArity("report", "exactly one file name");
      }
      Step step;
      step.type = StepType::REPORT;
      step.name = args[1];
      steps.push_back(step);
    } else if (command == "restart") {
      // Simulated reboot. Takes no arguments and must be followed by at least
      // one step: a resumed run that starts past the last step would idle
      // forever, which is a scenario authoring error worth catching at parse
      // time.
      if (!exactArgs(1)) {
        rejectArity("restart", "no arguments");
      }
      Step step;
      step.type = StepType::RESTART;
      steps.push_back(step);
    } else if (command == "action") {
      Step step;
      step.type = StepType::ACTION;
      std::getline(input, step.name);
      step.name = trim(step.name);
      if (step.name.empty()) {
        std::cerr << "action requires a name\n";
        std::exit(2);
      }
      std::string error;
      if (!parseScenarioAction(step.name, &step.action, &error)) {
        std::cerr << "Invalid simulator action '" << step.name << "': " << error << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "print") {
      if (!exactArgs(2)) {
        rejectArity("print", "exactly one key");
      }
      Step step;
      step.type = StepType::PRINT;
      step.name = args[1];
      steps.push_back(step);
    } else if (command == "assert") {
      if (!exactArgs(3)) {
        rejectArity("assert", "exactly a key and an expected value");
      }
      Step step;
      step.type = StepType::ASSERT;
      step.name = args[1];
      step.expected = args[2];
      steps.push_back(step);
    } else if (command == "assert-eventually" || command == "assert-eventually-virtual") {
      if (!exactArgs(4)) {
        rejectArity(command, "exactly TIMEOUT_MS KEY VALUE");
      }
      Step step;
      step.type = command == "assert-eventually" ? StepType::ASSERT_EVENTUALLY
                                                 : StepType::ASSERT_EVENTUALLY_VIRTUAL;
      step.timeoutMilliseconds = parseUnsigned(args[1]);
      step.name = args[2];
      step.expected = args[3];
      if (step.timeoutMilliseconds == 0 || step.timeoutMilliseconds > MAX_EVENTUAL_TIMEOUT_MS) {
        std::cerr << command << " timeout must be between 1 and " << MAX_EVENTUAL_TIMEOUT_MS
                  << " ms\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "xassert") {
      // Expected-fail assert. Documents a value the app SHOULD produce once a
      // pending product fix lands, without failing the run today. A mismatch
      // prints XFAIL and continues; a match fails the run so the follow-up fix
      // PR knows to promote the line back to a plain "assert". See sim/CLAUDE.md.
      //
      // "xassert board-varies KEY VALUE" is the one exception: a gap that is
      // already closed on some panels and open on others. One scenario file
      // runs on every board, so a match there is expected on the good panels
      // and must not fail the run. It still prints, so the line is visible.
      Step step;
      step.type = StepType::XASSERT;
      if (args.size() > 1 && args[1] == "board-varies") {
        if (!exactArgs(4)) {
          rejectArity("xassert board-varies", "exactly a key and an expected value");
        }
        step.boardVaries = true;
        step.name = args[2];
        step.expected = args[3];
      } else {
        if (!exactArgs(3)) {
          rejectArity("xassert", "exactly a key and an expected value");
        }
        step.name = args[1];
        step.expected = args[2];
      }
      steps.push_back(step);
    } else if (command == "assert_max") {
      // Numeric upper bound: the query value parsed as an integer must be at
      // most the expected value. Used for the redraw-storm probe, where a steady
      // page must hold its invalidation count low rather than at an exact value.
      if (!exactArgs(3)) {
        rejectArity("assert_max", "exactly a key and a maximum value");
      }
      Step step;
      step.type = StepType::ASSERT_MAX;
      step.name = args[1];
      step.expected = args[2];
      parseSigned(step.expected, "assert_max");
      steps.push_back(step);
    } else if (command == "assert_min") {
      // Numeric lower bound: the query value parsed as an integer must be at
      // least the expected value. Used to assert a page rendered widgets
      // (visible_objects >= 1) and that the bubble tracked a tilt in the right
      // direction without pinning an exact per-panel pixel value.
      if (!exactArgs(3)) {
        rejectArity("assert_min", "exactly a key and a minimum value");
      }
      Step step;
      step.type = StepType::ASSERT_MIN;
      step.name = args[1];
      step.expected = args[2];
      parseSigned(step.expected, "assert_min");
      steps.push_back(step);
    } else if (command == "exit") {
      if (!exactArgs(1)) {
        rejectArity("exit", "no arguments");
      }
      Step step;
      step.type = StepType::EXIT;
      steps.push_back(step);
    } else {
      std::cerr << "Unknown simulator script command: " << command << '\n';
      std::exit(2);
    }
  }

  for (const Step &step : steps) {
    if (step.type != StepType::ACTION || step.action.kind != scenario_action_kind_t::SIMPLE) {
      continue;
    }
    // The transport faults act on virtual BLE peers, so a scenario that uses
    // one without seeding a topology is a scripting error, not a silent no-op.
    if ((step.action.name == "ble-kill" || step.action.name == "ble-standby"
         || step.action.name == "ble-connect-fail" || step.action.name == "ble-connect-ok"
         || step.action.name == "ble-withhold-registration"
         || step.action.name == "ble-allow-registration")
        && (scenarioSettings.find("ble_peers") == scenarioSettings.end()
            || scenarioSettings.at("ble_peers") == "none")) {
      std::cerr << "Invalid simulator action '" << step.name
                << "': requires seed ble_peers <topology>\n";
      std::exit(2);
    }
  }

  if (!steps.empty() && steps.back().type == StepType::RESTART) {
    std::cerr << "restart must not be the final step\n";
    std::exit(2);
  }
}

int32_t parseSigned(const std::string &value, const char *name) {
  try {
    size_t parsed = 0;
    const long number = std::stol(value, &parsed, 10);
    if (parsed != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    if (number < std::numeric_limits<int32_t>::min()
        || number > std::numeric_limits<int32_t>::max()) {
      throw std::out_of_range("int32 range");
    }
    return static_cast<int32_t>(number);
  } catch (const std::exception &) {
    std::cerr << "Invalid " << name << ": " << value << '\n';
    std::exit(2);
  }
}

uint16_t batteryVoltage(const std::string &value) {
  const uint32_t voltage = parseUnsigned(value);
  if (voltage > 65535) {
    std::cerr << "Invalid battery_voltage: " << value << '\n';
    std::exit(2);
  }
  return static_cast<uint16_t>(voltage);
}

uint32_t parseUnsigned(const std::string &value, const char *option, uint32_t maximum) {
  try {
    if (value.empty() || value[0] == '+' || value[0] == '-') {
      throw std::invalid_argument("signed or empty unsigned value");
    }
    size_t parsed = 0;
    const unsigned long number = std::stoul(value, &parsed, 10);
    if (parsed != value.size() || number > maximum) {
      throw std::out_of_range("range");
    }
    return static_cast<uint32_t>(number);
  } catch (const std::exception &) {
    std::cerr << "Invalid " << option << ": " << value << '\n';
    std::exit(2);
  }
}

std::filesystem::path reportPath(const std::string &name) {
  std::filesystem::path path(name);
  if (!path.has_extension()) {
    path += ".json";
  }
  if (path.is_absolute() || name.find('/') != std::string::npos) {
    return path;
  }
  return std::filesystem::path(reportDirectory) / path;
}

std::string capturePath(const std::string &name) {
  const std::string filename =
      name.size() >= 4 && name.substr(name.size() - 4) == ".png" ? name : name + ".png";
  return captureDirectory + "/" + filename;
}

void saveBoolean(const std::string &name, Settings::type_t type) {
  const auto found = scenarioSettings.find(name);
  if (found != scenarioSettings.end()) {
    Settings::save<bool>(type, parseBool(found->second));
  }
}

void saveByte(const std::string &name, Settings::type_t type) {
  const auto found = scenarioSettings.find(name);
  if (found != scenarioSettings.end()) {
    Settings::save<uint8_t>(type, static_cast<uint8_t>(parseUnsigned(found->second)));
  }
}

const char *controlStateName(Control::state_t state) {
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

// Report a boolean setting as "1" or "0" so scenarios can assert persistence.
std::string settingBoolValue(const std::string &name) {
  static const std::map<std::string, Settings::type_t> booleans = {
      {"fauxny",            Settings::FAUXNY           },
      {"autoconnect",       Settings::AUTOCONNECT      },
      {"reconnect",         Settings::RECONNECT        },
      {"multiconnect",      Settings::MULTICONNECT     },
      {"companion",         Settings::COMPANION        },
#if defined(FURBLE_M5STICKS3)
      {"watchdog",          Settings::WATCHDOG         },
#endif
      {"gps",               Settings::GPS              },
      {"gps_nmea",          Settings::GPS_NMEA         },
      {"ir",                Settings::IR               },
      {"conn_saver",        Settings::CONN_SAVER       },
      {"preset_picker",     Settings::PRESET_PICKER    },
      {"show_title",        Settings::SHOW_TITLE       },
      {"tx_adaptive",       Settings::TX_ADAPTIVE      },
      {"recon_backoff",     Settings::RECON_BACKOFF    },
      {"auto_off_charging", Settings::AUTO_OFF_CHARGING},
      {"imu",               Settings::IMU              },
  };
  const auto found = booleans.find(name);
  if (found == booleans.end()) {
    return "";
  }
  return Settings::load<bool>(found->second) ? "1" : "0";
}

// Report a byte setting as its unsigned decimal value so scenarios can assert
// persistence of multi-choice settings such as the UI text size.
std::string settingByteValue(const std::string &name) {
  static const std::map<std::string, Settings::type_t> bytes = {
      {"text_size", Settings::TEXT_SIZE},
  };
  const auto found = bytes.find(name);
  if (found == bytes.end()) {
    return "";
  }
  return std::to_string(static_cast<unsigned>(Settings::load<uint8_t>(found->second)));
}

// Enforcement default is on. "seed liveness_check false" opts a scenario out
// of failing the run; detection and the violation counter keep running.
bool livenessEnforced(void) {
  const auto found = scenarioSettings.find("liveness_check");
  return found == scenarioSettings.end() || parseBool(found->second);
}

uint32_t livenessGraceMs(void) {
  const auto found = scenarioSettings.find("liveness_grace_ms");
  return found == scenarioSettings.end() ? LIVENESS_GRACE_DEFAULT_MS : parseUnsigned(found->second);
}

// The converse invariant the per-step asserts cannot watch continuously: a UI
// that presents Connected implies the camera links are actually up. Runs every
// driver tick on the UI task, reusing the exact ui.connected presentation
// check plus the per-camera link truth behind control.connected.
void checkLivenessInvariant(void) {
  if (scenarioUi == nullptr) {
    return;
  }

  auto &control = Control::getInstance();
  const size_t targets = control.getTargetCount();
  const size_t connected = control.getConnectedTargetCount();
  const bool presentsConnected = scenarioUi->simQueryState("connected") == "yes";
  const bool diverged = presentsConnected && connected < targets;
  if (!diverged) {
    livenessArmed = false;
    livenessLatched = false;
    return;
  }

  const uint32_t now = clockMillis();
  if (!livenessArmed) {
    livenessArmed = true;
    livenessDeadline = now + livenessGraceMs();
    return;
  }
  if (livenessLatched || !clockDeadlineReached(now, livenessDeadline)) {
    return;
  }

  livenessLatched = true;
  ++livenessViolations;
  if (livenessEnforced()) {
    std::cerr << "LIVENESS INVARIANT FAILED: UI shows Connected but only " << connected << " of "
              << targets << " camera links are live beyond the " << livenessGraceMs()
              << " ms grace period\n";
    std::cout.flush();
    requestExit(1);
    return;
  }
  std::cout << "liveness violation recorded (enforcement off): " << connected << " of " << targets
            << " camera links live\n";
}

// Transport faults. These act on the real MockNimBLE link behind a production
// Camera, so the observable is exactly the observable on hardware: the link is
// gone and whatever the production stack does next is what the scenario sees.
// There is no simulator-side control-state override any more.
//
//   ble-kill         sever every live link and leave the GAP disconnect event
//                    queued, so the camera keeps reporting connected over a
//                    link that is physically dead
//   ble-standby      run the standby drop of every flappy virtual peer: the
//                    peer re-arms its handshake failure budget, announces its
//                    power state and severs the link
//   ble-connect-fail make NimBLEClient::connect() fail at the transport
//   ble-connect-ok   let connects succeed again
//   ble-withhold-registration / ble-allow-registration
//                    make every Fujifilm peer answer the link but never
//                    confirm registration, so the production connect blocks
//                    in its registration wait
//
// Returns true when the action was one of these, so the caller does not also
// dispatch it into the UI.
bool applyTransportFaultAction(const scenario_action_t &action, bool *applied) {
  if (action.kind != scenario_action_kind_t::SIMPLE) {
    return false;
  }

  if (action.name == "ble-kill") {
    *applied = bleDropLink(-1, /*deliverCallback=*/false);
    return true;
  }
  if (action.name == "ble-standby") {
    *applied = blePeerStandbyDrop(-1);
    return true;
  }
  if (action.name == "ble-withhold-registration") {
    *applied = bleSetWithholdRegistration(true);
    return true;
  }
  if (action.name == "ble-allow-registration") {
    *applied = bleSetWithholdRegistration(false);
    return true;
  }
  if (action.name == "ble-connect-fail") {
    bleSetConnectFail(true);
    *applied = true;
    return true;
  }
  if (action.name == "ble-connect-ok") {
    bleSetConnectFail(false);
    *applied = true;
    return true;
  }

  return false;
}

const char *simActionResultName(UI::sim_action_result_t result) {
  switch (result) {
    case UI::sim_action_result_t::APPLIED:
      return "APPLIED";
    case UI::sim_action_result_t::VALID_NO_EFFECT:
      return "VALID_NO_EFFECT";
    case UI::sim_action_result_t::UNAVAILABLE:
      return "UNAVAILABLE";
    case UI::sim_action_result_t::INVALID:
      return "INVALID";
  }
  return "INVALID";
}

bool expectedSimActionResult(const scenario_action_t &action, UI::sim_action_result_t actual) {
  switch (action.expectation) {
    case scenario_action_expectation_t::APPLIED:
      return actual == UI::sim_action_result_t::APPLIED;
    case scenario_action_expectation_t::VALID_NO_EFFECT:
      return actual == UI::sim_action_result_t::VALID_NO_EFFECT;
    case scenario_action_expectation_t::UNAVAILABLE:
      return actual == UI::sim_action_result_t::UNAVAILABLE;
  }
  return false;
}

// Resolve an assertable state key to a string. UI keys run on the UI task, so
// LVGL reads stay single threaded. Control and camera keys read the shared
// state the real app maintains.
std::string queryValue(const std::string &key) {
  const auto prefixed = [&key](const char *prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return key.size() >= length && key.compare(0, length, prefix) == 0;
  };

  if (key == "ui.liveness_violations") {
    // Driver-owned counter: how many times the continuous liveness invariant
    // fired. With "seed liveness_check false" the run keeps going, so this
    // query proves the invariant would have failed the scenario.
    return std::to_string(livenessViolations);
  }
  if (prefixed("ui.")) {
    if (scenarioUi == nullptr) {
      return "";
    }
    return scenarioUi->simQueryState(key.substr(3).c_str());
  }
  if (prefixed("control.")) {
    auto &control = Control::getInstance();
    const std::string sub = key.substr(std::char_traits<char>::length("control."));
    if (sub == "state") {
      return controlStateName(control.getState());
    }
    if (sub == "connected") {
      return std::to_string(control.getConnectedTargetCount());
    }
    if (sub == "targets") {
      return std::to_string(control.getTargetCount());
    }
    // The internals the 2026-08-28 wedge was diagnosed from: a control task
    // stuck in disconnecting with a connect still in progress, and quarantined
    // targets that never drained. Production exposes them to the debug console;
    // the simulator reads the same snapshot.
    const auto debug = control.getDebugState();
    if (sub == "zombies") {
      return std::to_string(debug.zombieCount);
    }
    if (sub == "connect_in_progress") {
      return debug.connectInProgress ? "yes" : "no";
    }
    if (sub == "connect_abort") {
      return debug.connectAbort ? "yes" : "no";
    }
    if (sub == "reconnect_attempt") {
      return std::to_string(debug.reconnectAttempt);
    }
    if (sub == "reconnect_backoff") {
      return debug.reconnectBackoff ? "yes" : "no";
    }
    if (sub == "infinite_reconnect") {
      return debug.infiniteReconnect ? "yes" : "no";
    }
    if (sub == "connecting_camera") {
      return debug.connectingCamera;
    }
  }
  if (prefixed("camera.")) {
    const std::string sub = key.substr(std::char_traits<char>::length("camera."));
    if (sub == "count") {
      return std::to_string(CameraList::size());
    }
    if (sub == "shutter_presses") {
      return std::to_string(cameraShutterPresses());
    }
    if (sub == "shutter_releases") {
      return std::to_string(cameraShutterReleases());
    }
    if (sub == "focus_presses") {
      return std::to_string(cameraFocusPresses());
    }
    if (sub == "focus_releases") {
      return std::to_string(cameraFocusReleases());
    }
  }
  if (key == "scan.end_callbacks") {
    return std::to_string(Scan::getInstance().endCallbackCount());
  }
  if (key == "scan.advertisements") {
    return std::to_string(bleAdvertisementCount());
  }
  if (key == "ble.peers") {
    return std::to_string(blePeerCount());
  }
  if (key == "scan.start_probe_blocked") {
    return Scan::getInstance().startProbeBlocked() ? "1" : "0";
  }
  if (prefixed("gps.")) {
    auto &gps = Furble::GPS::getInstance();
    const std::string sub = key.substr(std::char_traits<char>::length("gps."));
    if (sub == "enabled") {
      return gps.isEnabled() ? "1" : "0";
    }
    if (sub == "source") {
      switch (gps.getSource()) {
        case Furble::GPS::SOURCE_UART:
          return "uart";
        case Furble::GPS::SOURCE_COMPANION:
          return "companion";
        case Furble::GPS::SOURCE_NONE:
          return "none";
      }
    }
    if (sub == "satellites") {
      return std::to_string(gps.getSatellites());
    }
    if (sub == "state") {
      return Furble::Sim::profilerGpsState();
    }
    if (sub == "degraded") {
      return gps.getCycleStatusSnapshot().degraded ? "1" : "0";
    }
    if (sub == "degraded_retries") {
      return std::to_string(gps.getCycleStatusSnapshot().retries);
    }
    if (sub.rfind("config.", 0) == 0) {
      const std::string field = sub.substr(7);
      const size_t dot = field.find('.');
      if (dot == std::string::npos) {
        std::cerr << "GPS config assert needs <index>.state or <index>.attempts\n";
        requestExit(2);
        return "";
      }
      size_t parsed = 0;
      size_t index = 0;
      try {
        const std::string indexText = field.substr(0, dot);
        if (indexText.empty() || indexText[0] == '+' || indexText[0] == '-') {
          throw std::invalid_argument("negative or empty index");
        }
        const unsigned long value = std::stoul(indexText, &parsed, 10);
        if (parsed != indexText.size()) {
          throw std::invalid_argument("trailing index");
        }
        index = static_cast<size_t>(value);
      } catch (const std::exception &) {
        std::cerr << "Invalid GPS config index: " << field.substr(0, dot) << '\n';
        requestExit(2);
        return "";
      }
      const auto status = gps.getConfigStatus();
      if (index >= status.size()) {
        return "";
      }
      const std::string property = field.substr(dot + 1);
      if (property == "state") {
        return Furble::GPS::configStateName(status[index].state);
      }
      if (property == "attempts") {
        return std::to_string(status[index].attempts);
      }
    }
  }
  if (key == "power.no_light_sleep") {
    return std::to_string(Power::getInstance().getCount(Power::LockType::NO_LIGHT_SLEEP));
  }
  if (key == "power.no_light_sleep_acquires") {
    return std::to_string(
        Power::getInstance().getStats(Power::LockType::NO_LIGHT_SLEEP).totalAcquires);
  }
  if (prefixed("uart.")) {
    const std::string sub = key.substr(std::char_traits<char>::length("uart."));
    const auto writes = furble_sim_uart_writes_snapshot();
    if (sub == "count") {
      return std::to_string(writes.size());
    }
    if (sub == "last") {
      if (writes.empty()) {
        return "";
      }
      std::string last = writes.back();
      while (!last.empty() && (last.back() == '\r' || last.back() == '\n')) {
        last.pop_back();
      }
      return last;
    }
  }
  if (prefixed("setting.")) {
    const std::string name = key.substr(std::char_traits<char>::length("setting."));
    std::string value = settingByteValue(name);
    if (value.empty()) {
      value = settingBoolValue(name);
    }
    if (value.empty()) {
      std::cerr << "Unknown setting for assert: " << name << '\n';
      requestExit(2);
      return "";
    }
    return value;
  }
  if (key == "platform.power_off") {
    return simulatedPowerOff ? "yes" : "no";
  }
  if (key == "platform.light_sleep") {
    return esp_pm_sim_light_sleep_allowed() ? "yes" : "no";
  }
  if (prefixed("platform.battery.")) {
    const std::string field = key.substr(std::char_traits<char>::length("platform.battery."));
    if (field == "level") {
      return std::to_string(simulatedBattery.level);
    }
    if (field == "voltage") {
      return std::to_string(simulatedBattery.voltage);
    }
    if (field == "current") {
      return std::to_string(simulatedBattery.current);
    }
    if (field == "charging") {
      return simulatedBattery.charging ? "yes" : "no";
    }
  }
  if (key == "platform.watchdog") {
    return watchdogState();
  }
  if (key == "platform.download_lock") {
    return downloadLockState();
  }
  if (key == "clock.ms") {
    return std::to_string(clockMillis());
  }

  std::cerr << "Unknown assert key: " << key << '\n';
  requestExit(2);
  return "";
}

}  // namespace

uint32_t livenessViolationCount(void) {
  return livenessViolations;
}

void preparePreferences(void) {
  if (scenarioName == "interactive") {
    return;
  }
  const std::filesystem::path path =
      std::filesystem::path(".pio") / ("furble-sim-preferences-" + scenarioName + ".bin");
  setenv("FURBLE_SIM_PREFS", path.string().c_str(), 1);
  // A resumed boot after a `restart` step keeps the preferences file: it is
  // the flash NVS the reboot must carry over. Only a fresh scenario run starts
  // from an empty store.
  if (!resumedBoot) {
    std::remove(path.c_str());
  }
}

void applyScenarioSettings(void) {
  saveByte("brightness", Settings::BRIGHTNESS);
  saveByte("inactivity", Settings::INACTIVITY);
  saveByte("display_off", Settings::DISPLAY_OFF);
  saveByte("gps_rate", Settings::GPS_RATE);
  saveByte("gps_constel", Settings::GPS_CONSTEL);
  saveByte("gps_power", Settings::GPS_POWER);
  saveByte("gps_duty", Settings::GPS_DUTY);
  saveByte("cpu_freq", Settings::CPU_FREQ);
  saveByte("tx_power", Settings::TX_POWER);
  saveByte("scan_mode", Settings::SCAN_MODE);
  saveByte("text_size", Settings::TEXT_SIZE);
  saveByte("auto_off", Settings::AUTO_OFF);
  saveByte("low_batt", Settings::LOW_BATT);
  saveByte("fb_output", Settings::FB_OUTPUT);
  const auto scanTimeout = scenarioSettings.find("scan_timeout");
  if (scanTimeout != scenarioSettings.end()) {
    Settings::save<uint32_t>(Settings::SCAN_TIMEOUT, parseUnsigned(scanTimeout->second));
  }
  saveBoolean("auto_off_charging", Settings::AUTO_OFF_CHARGING);
  saveBoolean("gps", Settings::GPS);
  saveBoolean("gps_nmea", Settings::GPS_NMEA);
  saveBoolean("fauxny", Settings::FAUXNY);
  saveBoolean("autoconnect", Settings::AUTOCONNECT);
  saveBoolean("reconnect", Settings::RECONNECT);
  saveBoolean("recon_backoff", Settings::RECON_BACKOFF);
  saveBoolean("sleep_conn", Settings::SLEEP_CONN);
  saveBoolean("boot_splash", Settings::BOOT_SPLASH);
#if defined(FURBLE_M5STICKS3)
  saveBoolean("watchdog", Settings::WATCHDOG);
#endif

  const auto batteryLevel = scenarioSettings.find("battery_level");
  const auto batteryVoltageSetting = scenarioSettings.find("battery_voltage");
  const auto batteryCurrent = scenarioSettings.find("battery_current");
  const auto batteryCharging = scenarioSettings.find("battery_charging");
  if (batteryLevel != scenarioSettings.end() || batteryVoltageSetting != scenarioSettings.end()
      || batteryCurrent != scenarioSettings.end() || batteryCharging != scenarioSettings.end()) {
    battery_reading_t reading = simulatedBattery;
    if (batteryLevel != scenarioSettings.end()) {
      const uint32_t level = parseUnsigned(batteryLevel->second);
      if (level > 100) {
        std::cerr << "Invalid battery_level: " << batteryLevel->second << '\n';
        requestExit(2);
        return;
      }
      reading.level = static_cast<uint8_t>(level);
    }
    if (batteryVoltageSetting != scenarioSettings.end()) {
      reading.voltage = batteryVoltage(batteryVoltageSetting->second);
    }
    if (batteryCurrent != scenarioSettings.end()) {
      reading.current = parseSigned(batteryCurrent->second, "battery_current");
    }
    if (batteryCharging != scenarioSettings.end()) {
      const std::string &value = batteryCharging->second;
      if (value != "0" && value != "1" && value != "true" && value != "false" && value != "yes"
          && value != "no" && value != "on" && value != "off") {
        std::cerr << "Invalid battery_charging: " << value << '\n';
        requestExit(2);
        return;
      }
      reading.charging = parseBool(value);
    }
    simulatedBattery = reading;
  }

  const auto uartMode = scenarioSettings.find("gps_uart_mode");
  if (uartMode != scenarioSettings.end()) {
    furble_sim_uart_set_mode(uartMode->second.c_str());
  }
  saveBoolean("imu", Settings::IMU);
  // Keep the host sensor surface in step with the setting used to construct
  // the UI. The SDL platform cannot initialize a physical IMU, so the shared
  // seam owns the enabled state for both page visibility and sensor reads.
  bool imu_sensor = scenarioSettingIsTrue("imu");
  const auto imu_sensor_setting = scenarioSettings.find("imu_sensor");
  if (imu_sensor_setting != scenarioSettings.end()) {
    imu_sensor = parseBool(imu_sensor_setting->second);
  }
  imuSetEnabled(imu_sensor);

  interval_t interval = Settings::load<Settings::INTERVAL>();
  bool interval_changed = false;
  const auto set_interval = [&](const char *name, SpinValue::nvs_t &value, SpinValue::unit_t unit) {
    const auto found = scenarioSettings.find(name);
    if (found != scenarioSettings.end()) {
      value = {static_cast<uint16_t>(parseUnsigned(found->second)), unit};
      interval_changed = true;
    }
  };
  set_interval("interval_count", interval.count, SpinValue::UNIT_NIL);
  set_interval("interval_delay", interval.delay, SpinValue::UNIT_SEC);
  set_interval("interval_shutter", interval.shutter, SpinValue::UNIT_MS);
  set_interval("interval_wait", interval.wait, SpinValue::UNIT_SEC);
  if (interval_changed) {
    Settings::save<Settings::INTERVAL>(interval);
  }

  const auto bulb_duration = scenarioSettings.find("bulb_duration");
  if (bulb_duration != scenarioSettings.end()) {
    Settings::save<Settings::BULB>(SpinValue::nvs_t {
        static_cast<uint16_t>(parseUnsigned(bulb_duration->second)), SpinValue::UNIT_SEC});
  }
}

bool scenarioSettingIsTrue(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const auto found = scenarioSettings.find(name);
  return found != scenarioSettings.end() && parseBool(found->second);
}

std::string scenarioSetting(const char *name, const char *fallback) {
  if (name == nullptr) {
    return fallback == nullptr ? std::string() : std::string(fallback);
  }
  const auto found = scenarioSettings.find(name);
  if (found == scenarioSettings.end()) {
    return fallback == nullptr ? std::string() : std::string(fallback);
  }
  return found->second;
}

void registerUI(UI *ui) {
  scenarioUi = ui;
}

battery_reading_t batteryReading(void) {
  return simulatedBattery;
}

void notePowerOff(void) {
  simulatedPowerOff = true;
}

void configure(int argc, char **argv) {
  requestedExit.store(-1);
  if (configured) {
    return;
  }
  configured = true;

  // Keep the exact invocation so a `restart` step can re-execute it.
  savedArguments.assign(argv, argv + argc);

  std::string script;
  bool rig = false;
  uint16_t rigPort = 6737;
  bool ignoreUuidMismatch = false;
  bool dropNotify = false;
  uint32_t delayMs = 0;
  bool fuzz = false;
  uint64_t fuzzSeed = 1;
  uint32_t fuzzSteps = 500;
  bool fuzzVerbose = false;
  bool fuzzRequested = false;
  bool seedOptionProvided = false;
  bool stepsOptionProvided = false;
  bool scriptOptionProvided = false;
  bool fuzzOptionProvided = false;
  bool fuzzVerboseProvided = false;
  bool helpRequested = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    const auto optionValue = [&](const char *option) {
      if (i + 1 >= argc || argv[i + 1][0] == '-') {
        std::cerr << option << " requires a value\n";
        std::exit(2);
      }
      return std::string(argv[++i]);
    };
    if (argument == "--script") {
      if (scriptOptionProvided) {
        std::cerr << "Duplicate --script option\n";
        std::exit(2);
      }
      scriptOptionProvided = true;
      script = optionValue("--script");
      if (script.empty()) {
        std::cerr << "--script requires a non-empty path\n";
        std::exit(2);
      }
    } else if (argument == "--fuzz") {
      if (fuzzOptionProvided) {
        std::cerr << "Duplicate --fuzz option\n";
        std::exit(2);
      }
      fuzzOptionProvided = true;
      fuzz = true;
      fuzzRequested = true;
    } else if (argument == "--seed") {
      if (seedOptionProvided) {
        std::cerr << "Duplicate --seed option\n";
        std::exit(2);
      }
      fuzz = true;
      fuzzRequested = true;
      seedOptionProvided = true;
      fuzzSeed = parseUnsigned64(optionValue("--seed"), "--seed");
    } else if (argument == "--fuzz-steps") {
      if (stepsOptionProvided) {
        std::cerr << "Duplicate --fuzz-steps option\n";
        std::exit(2);
      }
      fuzz = true;
      fuzzRequested = true;
      stepsOptionProvided = true;
      const uint64_t steps = parseUnsigned64(optionValue("--fuzz-steps"), "--fuzz-steps");
      if (steps == 0 || steps > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Invalid --fuzz-steps: " << steps << '\n';
        std::exit(2);
      }
      fuzzSteps = static_cast<uint32_t>(steps);
    } else if (argument == "--fuzz-verbose") {
      if (fuzzVerboseProvided) {
        std::cerr << "Duplicate --fuzz-verbose option\n";
        std::exit(2);
      }
      fuzzVerboseProvided = true;
      fuzzVerbose = true;
      fuzz = true;
      fuzzRequested = true;
    } else if (argument == "--capture-dir" || argument == "--out") {
      captureDirectory = optionValue(argument.c_str());
    } else if (argument == "--report-dir") {
      reportDirectory = optionValue("--report-dir");
    } else if (argument == "--rig") {
      rig = true;
    } else if (argument == "--rig-port") {
      const uint32_t port = parseUnsigned(optionValue("--rig-port"), "--rig-port", 65535);
      if (port == 0) {
        std::cerr << "Invalid --rig-port: 0\n";
        std::exit(2);
      }
      rigPort = static_cast<uint16_t>(port);
    } else if (argument == "--ignore-uuid-mismatch") {
      ignoreUuidMismatch = true;
    } else if (argument == "--drop-notify") {
      dropNotify = true;
    } else if (argument == "--delay-ms") {
      delayMs = parseUnsigned(optionValue("--delay-ms"), "--delay-ms",
                              std::numeric_limits<uint32_t>::max());
    } else if (argument == "--help") {
      helpRequested = true;
    } else {
      std::cerr << "Unknown simulator option: " << argument << '\n';
      std::exit(2);
    }
  }

  // Environment fallbacks let CI drive the fuzzer without argv edits. Explicit
  // command-line values are authoritative, including when a wrapper leaves
  // the corresponding fallback variable exported.
  if (const char *env = std::getenv("FURBLE_FUZZ_SEED"); env != nullptr) {
    if (!seedOptionProvided) {
      if (env[0] == '\0') {
        std::cerr << "FURBLE_FUZZ_SEED must not be empty\n";
        std::exit(2);
      }
      fuzz = true;
      fuzzRequested = true;
      fuzzSeed = parseUnsigned64(env, "FURBLE_FUZZ_SEED");
    }
  }
  if (const char *env = std::getenv("FURBLE_FUZZ_STEPS"); env != nullptr) {
    if (!stepsOptionProvided) {
      if (env[0] == '\0') {
        std::cerr << "FURBLE_FUZZ_STEPS must not be empty\n";
        std::exit(2);
      }
      fuzz = true;
      fuzzRequested = true;
      const uint64_t steps = parseUnsigned64(env, "FURBLE_FUZZ_STEPS");
      if (steps == 0 || steps > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Invalid FURBLE_FUZZ_STEPS: " << env << '\n';
        std::exit(2);
      }
      fuzzSteps = static_cast<uint32_t>(steps);
    }
  }

  if (!script.empty() && fuzzRequested) {
    std::cerr << "--script cannot be combined with fuzz options or FURBLE_FUZZ_*\n";
    std::exit(2);
  }

  if (helpRequested) {
    std::cout << "furble-sim [--script FILE] [--out DIR] [--report-dir DIR] [--rig] "
                 "[--rig-port PORT] [--ignore-uuid-mismatch] [--drop-notify] [--delay-ms MS] "
                 "[--fuzz] [--seed N] [--fuzz-steps N] [--fuzz-verbose]\n";
    std::exit(0);
  }

  rigConfigure(rig, rigPort, ignoreUuidMismatch, dropNotify, delayMs);

  if (fuzz) {
    scenarioName = "fuzz";
    fuzzConfigure(fuzzSeed, fuzzSteps, fuzzVerbose);
    return;
  }

  if (!script.empty()) {
    scenarioName = std::filesystem::path(script).stem().string();
    readScript(script);
    const auto clock = scenarioSettings.find("clock_ms");
    if (clock != scenarioSettings.end()) {
      setClockMillis(parseUnsigned(clock->second));
    }

    // A resumed boot continues the scenario after its `restart` step. Seeds
    // were reparsed above and are reapplied at this boot, exactly as boot-time
    // configuration is; everything else restarts from scratch.
    if (const char *resume = std::getenv(RESTART_STEP_ENV);
        resume != nullptr && resume[0] != '\0') {
      const uint32_t index = parseUnsigned(resume);
      // A value at or past the end would park the driver on no step at all.
      if (index == 0 || index >= steps.size()) {
        std::cerr << "Invalid " << RESTART_STEP_ENV << ": " << resume << '\n';
        std::exit(2);
      }
      stepIndex = index;
      resumedBoot = true;
      // Consume it. A leftover export in the caller's environment would make
      // every later scenario resume mid-script against a preserved store, so
      // the variable lives exactly one boot and a `restart` step sets it again.
      unsetenv(RESTART_STEP_ENV);
    }
  }
}

void setBackTarget(Furble::UI *ui) {
  backTarget = ui;
}

void startProfiler(void) {
  profilerBegin(scenarioName.c_str());
}

void driverTick(void) {
  // Keep the continuous liveness check ahead of the fuzzer's phase dispatcher.
  // A fuzz settle or finish phase must not create a blind spot for a sustained
  // false-connected presentation.
  checkLivenessInvariant();

  if (fuzzActive()) {
    fuzzTick(scenarioUi);
    return;
  }

  if (stepIndex >= steps.size()) {
    return;
  }

  const uint32_t now = clockMillis();
  if (pressedKey != SDLK_UNKNOWN) {
    if (!clockDeadlineReached(now, releaseAt)) {
      return;
    }
    pushKey(pressedKey, false);
    pressedKey = SDLK_UNKNOWN;
    ++stepIndex;
    return;
  }
  Step &step = steps[stepIndex];
  switch (step.type) {
    case StepType::WAIT:
      if (!waiting) {
        waitUntil = now + step.milliseconds;
        waiting = true;
      } else if (clockDeadlineReached(now, waitUntil)) {
        // Give joinable production tasks one final host scheduling quantum
        // after virtual time reaches the deadline. Without this handoff a
        // GPS retry deadline can be observed by the UI before serviceCycle()
        // gets to run, making boundary assertions race the worker.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        waiting = false;
        ++stepIndex;
      }
      break;

    case StepType::STALL:
      // Advance scenario time without returning to Platform::update. This
      // models a wedged UI task, so the virtual PM1 watchdog cannot be fed.
      advanceClock(step.milliseconds);
      // The UI loop resumes once to process the next assertion. Prevent that
      // bookkeeping cycle from feeding the retained PMIC: on hardware the
      // watchdog would have expired asynchronously during the stall.
      suppressNextWatchdogFeed();
      ++stepIndex;
      break;

    case StepType::KEY:
      pushKey(step.key, true);
      pressedKey = step.key;
      releaseAt = now + 80;
      break;

    case StepType::BTN:
      // Route the press through the UI so it runs the board's real button->nav
      // path (the same handlers furble uses on hardware), not the SDL panel's
      // emulated GPIOs, which do not reach the UI in a headless run.
      if (scenarioUi == nullptr) {
        std::cerr << "Scenario button ran before the UI was ready: " << step.name << '\n';
        requestExit(1);
        return;
      }
      if (!scenarioUi->simPressButton(step.name.c_str(), step.hold)) {
        std::cerr << "Simulator button not available on this board: " << step.name << '\n';
        requestExit(1);
        return;
      }
      ++stepIndex;
      break;

    case StepType::CAPTURE:
      if (!captureFrame(capturePath(step.name))) {
        std::cerr << "Could not capture simulator frame: " << step.name << '\n';
        requestExit(1);
        return;
      }
      std::cout << "Captured " << capturePath(step.name) << '\n';
      ++stepIndex;
      break;

    case StepType::UART_DUMP:
      // Print every captured GPS UART command, assertable by the caller.
      for (const auto &payload : furble_sim_uart_take_writes()) {
        std::string line = payload;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
          line.pop_back();
        }
        std::cout << "uart-tx " << line << '\n';
      }
      ++stepIndex;
      break;

    case StepType::UART_MODE:
      furble_sim_uart_set_mode(step.name.c_str());
      ++stepIndex;
      break;

    case StepType::UART_EVENT:
      furble_sim_uart_inject_event(step.name.c_str());
      ++stepIndex;
      break;

    case StepType::GPS_RESTART:
      Furble::GPS::getInstance().restart(step.name == "hot" ? 0 : step.name == "warm" ? 1 : 2);
      ++stepIndex;
      break;

    case StepType::HOME:
      if (backTarget == nullptr || !backTarget->simulatorHome()) {
        std::cerr << "Could not navigate home in simulator\n";
        requestExit(1);
        return;
      }
      ++stepIndex;
      break;

    case StepType::BACK:
      if (backTarget == nullptr || !backTarget->simulatorBack()) {
        std::cerr << "Could not navigate back in simulator\n";
        requestExit(1);
        return;
      }
      ++stepIndex;
      break;

    case StepType::REPORT:
    {
      const std::filesystem::path path = reportPath(step.name);
      const std::string reportName = path.stem().string();
      profilerWriteReport(path.string().c_str(), reportName.c_str());
      profilerResetWindow();
      std::cout << "Reported " << path.string() << '\n';
      ++stepIndex;
      break;
    }

    case StepType::RESTART:
    {
      // Simulated reboot. In-process teardown cannot model it honestly: the UI,
      // LVGL, and every singleton would survive, so the scenario would prove
      // nothing about what really crosses flash. Re-executing the binary wipes
      // all RAM state exactly as esp_restart() does while the preferences file
      // persists exactly as flash does, and the resumed process parses the same
      // script and continues at the step after this one.
      //
      // The re-exec itself happens in main(), after the plan 158 shutdown has
      // stopped and joined every simulator task and closed the panel. Execing
      // from here would tear the process image out from under running tasks,
      // which is a crash, not a reboot.
      const std::string next = std::to_string(stepIndex + 1);
      setenv(RESTART_STEP_ENV, next.c_str(), 1);
      std::cout << "restart: rebooting simulator, resuming at step " << next << '\n';
      std::cout.flush();
      // Advance past this step so a tick racing the shutdown cannot run it
      // twice. The resumed process takes its index from the environment.
      ++stepIndex;
      restartPending.store(true);
      return;
    }

    case StepType::ACTION:
    {
      if (scenarioUi == nullptr) {
        std::cerr << "Scenario action ran before the UI was ready: " << step.name << '\n';
        requestExit(1);
        return;
      }
      UI::sim_action_result_t result = UI::sim_action_result_t::INVALID;
      if (step.action.kind == scenario_action_kind_t::BATTERY) {
        simulatedBattery = {step.action.batteryLevel, step.action.batteryVoltage,
                            step.action.batteryCurrent, step.action.batteryCharging};
        result = UI::sim_action_result_t::APPLIED;
      } else if (bool applied = false; applyTransportFaultAction(step.action, &applied)) {
        result =
            applied ? UI::sim_action_result_t::APPLIED : UI::sim_action_result_t::VALID_NO_EFFECT;
      } else {
        result = scenarioUi->simScenarioAction(step.action);
      }
      if (!expectedSimActionResult(step.action, result)) {
        std::cerr << "Simulator action '" << step.name << "' returned "
                  << simActionResultName(result) << ", expected "
                  << (step.action.expectation == scenario_action_expectation_t::APPLIED ? "APPLIED"
                      : step.action.expectation == scenario_action_expectation_t::VALID_NO_EFFECT
                          ? "VALID_NO_EFFECT"
                          : "UNAVAILABLE")
                  << '\n';
        requestExit(result == UI::sim_action_result_t::INVALID ? 2 : 1);
        return;
      }
      ++stepIndex;
      break;
    }

    case StepType::ASSERT:
    {
      const std::string actual = queryValue(step.name);
      if (actual != step.expected) {
        std::cerr << "ASSERT FAILED: " << step.name << " expected '" << step.expected << "' got '"
                  << actual << "'\n";
        std::cout.flush();
        requestExit(1);
        return;
      }
      std::cout << "assert ok: " << step.name << " = " << actual << '\n';
      ++stepIndex;
      break;
    }

    case StepType::ASSERT_EVENTUALLY:
    {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(step.timeoutMilliseconds);
      std::string actual;
      for (;;) {
        actual = queryValue(step.name);
        if (actual == step.expected) {
          std::cout << "assert-eventually ok: " << step.name << " = " << actual << '\n';
          ++stepIndex;
          break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          std::cerr << "ASSERT-EVENTUALLY FAILED: " << step.name << " expected '" << step.expected
                    << "' got '" << actual << "' after " << step.timeoutMilliseconds << " ms\n";
          std::cout.flush();
          requestExit(1);
          return;
        }
        // This is a host-side observation wait. It leaves the UI task blocked
        // while allowing background simulator tasks to process the state that
        // the preceding virtual-time steps made due.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      break;
    }

    case StepType::ASSERT_EVENTUALLY_VIRTUAL:
    {
      if (!waiting) {
        waitUntil = now + step.timeoutMilliseconds;
        waiting = true;
      }

      const std::string actual = queryValue(step.name);
      if (actual == step.expected) {
        std::cout << "assert-eventually-virtual ok: " << step.name << " = " << actual << '\n';
        waiting = false;
        ++stepIndex;
        break;
      }
      if (clockDeadlineReached(now, waitUntil)) {
        std::cerr << "ASSERT-EVENTUALLY-VIRTUAL FAILED: " << step.name << " expected '"
                  << step.expected << "' got '" << actual << "' after " << step.timeoutMilliseconds
                  << " virtual ms\n";
        std::cout.flush();
        waiting = false;
        requestExit(1);
      }
      break;
    }

    case StepType::XASSERT:
    {
      // Expected-fail assertion: a known gap awaiting a separate product fix.
      // Asymmetric, like FURBLE_FUZZ_XFAIL_SEEDS. A mismatch is the documented
      // gap, so it prints and the run continues and CI stays green. A match
      // means the gap closed, and that has to be promoted back to a hard assert
      // deliberately rather than sitting as a silently passing xassert nobody
      // revisits, so it fails the run and says so.
      const std::string actual = queryValue(step.name);
      if (actual != step.expected) {
        std::cout << "XFAIL (WILL_FAIL): " << step.name << " expected '" << step.expected
                  << "' got '" << actual << "'\n";
      } else if (step.boardVaries) {
        std::cout << "XPASS (board-varies): " << step.name << " = " << actual
                  << ". This panel is already correct; the gap remains on another.\n";
      } else {
        std::cerr << "XPASS: " << step.name << " = " << actual
                  << ". The documented gap is closed; promote this xassert back to assert.\n";
        std::cout.flush();
        requestExit(1);
        return;
      }
      ++stepIndex;
      break;
    }

    case StepType::ASSERT_MAX:
    {
      const std::string actual = queryValue(step.name);
      long actualValue = 0;
      long maxValue = 0;
      try {
        actualValue = std::stol(actual);
        maxValue = std::stol(step.expected);
      } catch (const std::exception &) {
        std::cerr << "ASSERT_MAX FAILED: " << step.name << " non-numeric (got '" << actual
                  << "', max '" << step.expected << "')\n";
        std::cout.flush();
        requestExit(1);
        return;
      }
      if (actualValue > maxValue) {
        std::cerr << "ASSERT_MAX FAILED: " << step.name << " expected <= " << maxValue << " got "
                  << actualValue << '\n';
        std::cout.flush();
        requestExit(1);
        return;
      }
      std::cout << "assert ok: " << step.name << " = " << actualValue << " (<= " << maxValue
                << ")\n";
      ++stepIndex;
      break;
    }

    case StepType::ASSERT_MIN:
    {
      const std::string actual = queryValue(step.name);
      long actualValue = 0;
      long minValue = 0;
      try {
        actualValue = std::stol(actual);
        minValue = std::stol(step.expected);
      } catch (const std::exception &) {
        std::cerr << "ASSERT_MIN FAILED: " << step.name << " non-numeric (got '" << actual
                  << "', min '" << step.expected << "')\n";
        std::cout.flush();
        requestExit(1);
        return;
      }
      if (actualValue < minValue) {
        std::cerr << "ASSERT_MIN FAILED: " << step.name << " expected >= " << minValue << " got "
                  << actualValue << '\n';
        std::cout.flush();
        requestExit(1);
        return;
      }
      std::cout << "assert ok: " << step.name << " = " << actualValue << " (>= " << minValue
                << ")\n";
      ++stepIndex;
      break;
    }

    case StepType::PRINT:
      std::cout << "state " << step.name << " = " << queryValue(step.name) << '\n';
      ++stepIndex;
      break;

    case StepType::EXIT:
      std::cout.flush();
      requestExit(0);
      return;
  }
}

void requestExit(int result) {
  int unset = -1;
  requestedExit.compare_exchange_strong(unset, result);
}

void requestFailureExit(void) {
  int result = requestedExit.load();
  while (result == -1 || result == 0) {
    if (requestedExit.compare_exchange_weak(result, 1)) {
      return;
    }
  }
}

bool exitRequested(void) {
  return requestedExit.load() >= 0 || restartPending.load();
}

int exitResult(void) {
  const int result = requestedExit.load();
  return result < 0 ? 0 : result;
}

bool restartRequested(void) {
  return restartPending.load();
}

void restartProcess(void) {
  std::cout.flush();
  std::cerr.flush();
  std::vector<char *> arguments;
  arguments.reserve(savedArguments.size() + 1);
  for (auto &argument : savedArguments) {
    arguments.push_back(argument.data());
  }
  arguments.push_back(nullptr);
  execvp(arguments[0], arguments.data());
  std::cerr << "restart failed: execvp: " << std::strerror(errno) << '\n';
  std::_Exit(1);
}

bool connectShouldFail(void) {
  return scenarioSettingIsTrue("connect_fail");
}

}  // namespace Furble::Sim
