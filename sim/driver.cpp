#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include <driver/uart.h>

#include "CameraList.h"
#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "capture.h"
#include "clock.h"
#include "driver.h"
#include "power_profiler.h"

namespace Furble::Sim {
namespace {

enum class StepType {
  WAIT,
  KEY,
  BTN,
  CAPTURE,
  UART_DUMP,
  HOME,
  BACK,
  REPORT,
  ACTION,
  ASSERT,
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
};

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
    return static_cast<uint32_t>(std::stoul(value));
  } catch (...) {
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

    if (command == "seed") {
      std::string name;
      std::string value;
      input >> name >> value;
      if (name.empty() || value.empty()) {
        std::cerr << "seed requires a name and value\n";
        std::exit(2);
      }
      scenarioSettings[name] = value;
      continue;
    }

    if (command == "wait" || command == "advance") {
      Step step;
      step.type = StepType::WAIT;
      input >> step.milliseconds;
      steps.push_back(step);
    } else if (command == "key" || command == "press") {
      std::string name;
      input >> name;
      Step step;
      step.type = StepType::KEY;
      step.key = keyCode(name);
      if (step.key == SDLK_UNKNOWN) {
        std::cerr << "Unknown simulator key: " << name << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "btn" || command == "button") {
      // Press a physical button by name: a, b, c or pwr. An optional second
      // token "hold"/"long" selects the left-button long-press escape. Absent
      // it, the button taps. The name is validated against the board's button
      // set here so pressing an absent button (BtnC on a Stick, BtnPWR on a
      // Core) fails at parse time.
      std::string name;
      input >> name;
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (!buttonKnown(name)) {
        std::cerr << "Unknown or unavailable simulator button for this board: " << name << '\n';
        std::exit(2);
      }
      Step step;
      step.type = StepType::BTN;
      step.name = name;
      std::string hold;
      input >> hold;
      if (hold == "hold" || hold == "long") {
        step.hold = true;
      } else if (!hold.empty()) {
        std::cerr << "Unknown simulator button modifier: " << hold << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "capture") {
      Step step;
      step.type = StepType::CAPTURE;
      input >> step.name;
      steps.push_back(step);
    } else if (command == "uart-dump") {
      Step step;
      step.type = StepType::UART_DUMP;
      steps.push_back(step);
    } else if (command == "home") {
      Step step;
      step.type = StepType::HOME;
      steps.push_back(step);
    } else if (command == "back") {
      Step step;
      step.type = StepType::BACK;
      steps.push_back(step);
    } else if (command == "report") {
      Step step;
      step.type = StepType::REPORT;
      input >> step.name;
      if (step.name.empty()) {
        std::cerr << "report requires a file name\n";
        std::exit(2);
      }
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
      steps.push_back(step);
    } else if (command == "print") {
      Step step;
      step.type = StepType::PRINT;
      input >> step.name;
      if (step.name.empty()) {
        std::cerr << "print requires a key\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "assert") {
      Step step;
      step.type = StepType::ASSERT;
      input >> step.name;
      input >> step.expected;
      if (step.name.empty() || step.expected.empty()) {
        std::cerr << "assert requires a key and an expected value\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "exit") {
      Step step;
      step.type = StepType::EXIT;
      steps.push_back(step);
    } else {
      std::cerr << "Unknown simulator script command: " << command << '\n';
      std::exit(2);
    }
  }
}

uint32_t parseUnsigned(const std::string &value, const char *option, uint32_t maximum) {
  try {
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
      {"fauxny",        Settings::FAUXNY       },
      {"autoconnect",   Settings::AUTOCONNECT  },
      {"reconnect",     Settings::RECONNECT    },
      {"multiconnect",  Settings::MULTICONNECT },
      {"companion",     Settings::COMPANION    },
#if defined(FURBLE_M5STICKS3)
      {"watchdog",      Settings::WATCHDOG     },
#endif
      {"gps",           Settings::GPS          },
      {"gps_nmea",      Settings::GPS_NMEA     },
      {"ir",            Settings::IR           },
      {"conn_saver",    Settings::CONN_SAVER   },
      {"preset_picker", Settings::PRESET_PICKER},
      {"show_title",    Settings::SHOW_TITLE   },
      {"tx_adaptive",   Settings::TX_ADAPTIVE  },
      {"recon_backoff", Settings::RECON_BACKOFF},
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

// Resolve an assertable state key to a string. UI keys run on the UI task, so
// LVGL reads stay single threaded. Control and camera keys read the shared
// state the real app maintains.
std::string queryValue(const std::string &key) {
  const auto prefixed = [&key](const char *prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return key.size() >= length && key.compare(0, length, prefix) == 0;
  };

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
  }
  if (prefixed("camera.")) {
    const std::string sub = key.substr(std::char_traits<char>::length("camera."));
    if (sub == "shutter_presses") {
      return std::to_string(cameraShutterPresses());
    }
    if (sub == "shutter_releases") {
      return std::to_string(cameraShutterReleases());
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
      std::exit(2);
    }
    return value;
  }

  std::cerr << "Unknown assert key: " << key << '\n';
  std::exit(2);
}

}  // namespace

void preparePreferences(void) {
  if (scenarioName == "interactive") {
    return;
  }
  const std::filesystem::path path =
      std::filesystem::path(".pio") / ("furble-sim-preferences-" + scenarioName + ".bin");
  setenv("FURBLE_SIM_PREFS", path.string().c_str(), 1);
  std::remove(path.c_str());
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
  saveBoolean("gps", Settings::GPS);
  saveBoolean("gps_nmea", Settings::GPS_NMEA);
  saveBoolean("fauxny", Settings::FAUXNY);
  saveBoolean("autoconnect", Settings::AUTOCONNECT);
  saveBoolean("reconnect", Settings::RECONNECT);
  saveBoolean("sleep_conn", Settings::SLEEP_CONN);

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
}

bool scenarioSettingIsTrue(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const auto found = scenarioSettings.find(name);
  return found != scenarioSettings.end() && parseBool(found->second);
}

void registerUI(UI *ui) {
  scenarioUi = ui;
}

void configure(int argc, char **argv) {
  if (configured) {
    return;
  }
  configured = true;

  std::string script;
  bool rig = false;
  uint16_t rigPort = 6737;
  bool ignoreUuidMismatch = false;
  bool dropNotify = false;
  uint32_t delayMs = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--script" && i + 1 < argc) {
      script = argv[++i];
    } else if ((argument == "--capture-dir" || argument == "--out") && i + 1 < argc) {
      captureDirectory = argv[++i];
    } else if (argument == "--report-dir" && i + 1 < argc) {
      reportDirectory = argv[++i];
    } else if (argument == "--rig") {
      rig = true;
    } else if (argument == "--rig-port" && i + 1 < argc) {
      const uint32_t port = parseUnsigned(argv[++i], "--rig-port", 65535);
      if (port == 0) {
        std::cerr << "Invalid --rig-port: 0\n";
        std::exit(2);
      }
      rigPort = static_cast<uint16_t>(port);
    } else if (argument == "--ignore-uuid-mismatch") {
      ignoreUuidMismatch = true;
    } else if (argument == "--drop-notify") {
      dropNotify = true;
    } else if (argument == "--delay-ms" && i + 1 < argc) {
      delayMs = parseUnsigned(argv[++i], "--delay-ms", std::numeric_limits<uint32_t>::max());
    } else if (argument == "--help") {
      std::cout << "furble-sim [--script FILE] [--out DIR] [--report-dir DIR] [--rig] "
                   "[--rig-port PORT] [--ignore-uuid-mismatch] [--drop-notify] [--delay-ms MS]\n";
      std::exit(0);
    }
  }

  rigConfigure(rig, rigPort, ignoreUuidMismatch, dropNotify, delayMs);

  if (!script.empty()) {
    scenarioName = std::filesystem::path(script).stem().string();
    readScript(script);
  }
}

void setBackTarget(Furble::UI *ui) {
  backTarget = ui;
}

void startProfiler(void) {
  profilerBegin(scenarioName.c_str());
}

void driverTick(void) {
  if (stepIndex >= steps.size()) {
    return;
  }

  const uint32_t now = clockMillis();
  if (pressedKey != SDLK_UNKNOWN) {
    if (now < releaseAt) {
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
      } else if (now >= waitUntil) {
        waiting = false;
        ++stepIndex;
      }
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
        std::exit(1);
      }
      if (!scenarioUi->simPressButton(step.name.c_str(), step.hold)) {
        std::cerr << "Simulator button not available on this board: " << step.name << '\n';
        std::exit(1);
      }
      ++stepIndex;
      break;

    case StepType::CAPTURE:
      if (!captureFrame(capturePath(step.name))) {
        std::cerr << "Could not capture simulator frame: " << step.name << '\n';
        std::exit(1);
      }
      std::cout << "Captured " << capturePath(step.name) << '\n';
      ++stepIndex;
      break;

    case StepType::UART_DUMP:
      // Print every captured GPS UART command, assertable by the caller.
      for (const auto &payload : furble_sim_uart_writes()) {
        std::string line = payload;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
          line.pop_back();
        }
        std::cout << "uart-tx " << line << '\n';
      }
      furble_sim_uart_clear_writes();
      ++stepIndex;
      break;

    case StepType::HOME:
      if (backTarget == nullptr || !backTarget->simulatorHome()) {
        std::cerr << "Could not navigate home in simulator\n";
        std::exit(1);
      }
      ++stepIndex;
      break;

    case StepType::BACK:
      if (backTarget == nullptr || !backTarget->simulatorBack()) {
        std::cerr << "Could not navigate back in simulator\n";
        std::exit(1);
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

    case StepType::ACTION:
      if (scenarioUi == nullptr) {
        std::cerr << "Scenario action ran before the UI was ready: " << step.name << '\n';
        std::exit(1);
      }
      scenarioUi->simScenarioAction(step.name.c_str());
      ++stepIndex;
      break;

    case StepType::ASSERT:
    {
      const std::string actual = queryValue(step.name);
      if (actual != step.expected) {
        std::cerr << "ASSERT FAILED: " << step.name << " expected '" << step.expected << "' got '"
                  << actual << "'\n";
        std::cout.flush();
        // Skip host teardown so the assertion exit code is not masked by an
        // abort from background sim threads unwinding their mutexes.
        std::_Exit(1);
      }
      std::cout << "assert ok: " << step.name << " = " << actual << '\n';
      ++stepIndex;
      break;
    }

    case StepType::PRINT:
      std::cout << "state " << step.name << " = " << queryValue(step.name) << '\n';
      ++stepIndex;
      break;

    case StepType::EXIT:
      // Flush before the teardown-free exit so buffered assert and print lines
      // reach the log when stdout is a pipe rather than a terminal.
      std::cout.flush();
      std::_Exit(0);
  }
}

bool connectShouldFail(void) {
  return scenarioSettingIsTrue("connect_fail");
}

}  // namespace Furble::Sim
