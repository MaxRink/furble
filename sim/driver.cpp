#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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

#include <SDL2/SDL.h>
#include <lgfx/v1/platforms/sdl/common.hpp>

#include <driver/uart.h>

#include "CameraList.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "Scan.h"
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
  ACTION,
  ASSERT,
  ASSERT_EVENTUALLY,
  XASSERT,
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
battery_reading_t simulatedBattery = {80, 4000, 0, false};
bool simulatedPowerOff = false;

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

bool booleanSeedValue(const std::string &value) {
  return value == "0" || value == "1" || value == "true" || value == "false" || value == "yes"
         || value == "no" || value == "on" || value == "off";
}

void validateSeed(const std::string &name, const std::string &value) {
  if (name == "clock_ms") {
    parseUnsigned(value);
    return;
  }

  constexpr const char *byteSeeds[] = {
      "brightness", "inactivity", "display_off", "gps_rate", "gps_constel",
      "gps_power",  "gps_duty",   "cpu_freq",    "tx_power", "scan_mode",
      "text_size",  "auto_off",   "low_batt",
  };
  if (std::find(std::begin(byteSeeds), std::end(byteSeeds), name) != std::end(byteSeeds)) {
    if (parseUnsigned(value) > std::numeric_limits<uint8_t>::max()) {
      std::cerr << "Invalid " << name << ": " << value << '\n';
      std::exit(2);
    }
    return;
  }

  constexpr const char *booleanSeeds[] = {
      "gps",       "gps_nmea",     "fauxny",           "autoconnect",
      "reconnect", "sleep_conn",   "boot_splash",      "connect_fail",
      "no_touch",  "saved_camera", "scan_start_probe", "scan_distinct",
      "auto_off_charging",
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
  } else if (name == "gps_uart_mode") {
    if (value != "ack" && value != "nack" && value != "timeout" && value != "malformed"
        && value != "partial" && value != "write-error") {
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

    if (command == "seed") {
      std::string name;
      std::string value;
      input >> name >> value;
      std::string extra;
      input >> extra;
      if (name.empty() || value.empty() || !extra.empty()) {
        std::cerr << "seed requires exactly a name and value\n";
        std::exit(2);
      }
      validateSeed(name, value);
      scenarioSettings[name] = value;
      continue;
    }

    if (command == "wait" || command == "advance") {
      Step step;
      step.type = StepType::WAIT;
      input >> step.milliseconds;
      steps.push_back(step);
    } else if (command == "stall") {
      Step step;
      step.type = StepType::STALL;
      input >> step.milliseconds;
      if (step.milliseconds == 0) {
        std::cerr << "stall requires a non-zero duration\n";
        std::exit(2);
      }
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
    } else if (command == "uart-mode" || command == "gps-uart") {
      Step step;
      step.type = StepType::UART_MODE;
      input >> step.name;
      if (step.name != "ack" && step.name != "nack" && step.name != "timeout"
          && step.name != "malformed" && step.name != "partial" && step.name != "write-error") {
        std::cerr << "uart-mode requires ack, nack, timeout, malformed, partial or write-error\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "uart-event" || command == "gps-uart-event") {
      Step step;
      step.type = StepType::UART_EVENT;
      input >> step.name;
      if (step.name != "data" && step.name != "fifo" && step.name != "buffer"
          && step.name != "break" && step.name != "parity" && step.name != "frame"
          && step.name != "pattern") {
        std::cerr << "uart-event requires data, fifo, buffer, break, parity, frame or pattern\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "gps-restart") {
      Step step;
      step.type = StepType::GPS_RESTART;
      input >> step.name;
      if (step.name != "hot" && step.name != "warm" && step.name != "cold") {
        std::cerr << "gps-restart requires hot, warm or cold\n";
        std::exit(2);
      }
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
    } else if (command == "assert-eventually") {
      Step step;
      step.type = StepType::ASSERT_EVENTUALLY;
      std::string timeout;
      input >> timeout >> step.name >> step.expected;
      std::string extra;
      input >> extra;
      if (timeout.empty() || step.name.empty() || step.expected.empty() || !extra.empty()) {
        std::cerr << "assert-eventually requires TIMEOUT_MS KEY VALUE with no trailing values\n";
        std::exit(2);
      }
      step.timeoutMilliseconds = parseUnsigned(timeout);
      if (step.timeoutMilliseconds == 0 || step.timeoutMilliseconds > MAX_EVENTUAL_TIMEOUT_MS) {
        std::cerr << "assert-eventually timeout must be between 1 and " << MAX_EVENTUAL_TIMEOUT_MS
                  << " ms\n";
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "xassert") {
      // Expected-fail assert. Documents a value the app SHOULD produce once a
      // pending product fix lands, without failing the run today. A mismatch
      // prints XFAIL and continues; a match prints XPASS so the follow-up fix PR
      // knows to promote the line back to a plain "assert". See sim/CLAUDE.md.
      Step step;
      step.type = StepType::XASSERT;
      input >> step.name;
      input >> step.expected;
      if (step.name.empty() || step.expected.empty()) {
        std::cerr << "xassert requires a key and an expected value\n";
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

bool parseBatteryAction(const std::string &action) {
  std::istringstream input(action);
  std::string command;
  input >> command;
  if (command != "battery") {
    return false;
  }

  std::string levelText;
  std::string voltageText;
  std::string currentText;
  std::string chargingText;
  if (!(input >> levelText >> voltageText >> currentText >> chargingText)) {
    std::cerr << "action battery requires level voltage current charging\n";
    std::exit(2);
  }

  const uint32_t level = parseUnsigned(levelText);
  if (level > 100) {
    std::cerr << "Invalid battery level: " << levelText << '\n';
    std::exit(2);
  }
  const int32_t current = parseSigned(currentText, "battery_current");
  const bool charging = parseBool(chargingText);
  if (chargingText != "0" && chargingText != "1" && chargingText != "true"
      && chargingText != "false" && chargingText != "yes" && chargingText != "no"
      && chargingText != "on" && chargingText != "off") {
    std::cerr << "Invalid battery charging flag: " << chargingText << '\n';
    std::exit(2);
  }
  std::string extra;
  if (input >> extra) {
    std::cerr << "action battery has unexpected trailing value: " << extra << '\n';
    std::exit(2);
  }

  simulatedBattery = {static_cast<uint8_t>(level), batteryVoltage(voltageText), current, charging};
  return true;
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
      {"auto_off_charging", Settings::AUTO_OFF_CHARGING},
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
    if (sub.rfind("config.", 0) == 0) {
      const std::string field = sub.substr(7);
      const size_t dot = field.find('.');
      if (dot == std::string::npos) {
        std::cerr << "GPS config assert needs <index>.state or <index>.attempts\n";
        std::exit(2);
      }
      const size_t index = static_cast<size_t>(std::stoul(field.substr(0, dot)));
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
      std::exit(2);
    }
    return value;
  }
  if (key == "platform.power_off") {
    return simulatedPowerOff ? "yes" : "no";
  }
  if (key == "platform.light_sleep") {
    return esp_pm_sim_light_sleep_allowed() ? "yes" : "no";
  }
  if (key == "platform.deep_sleep") {
    // The simulator has no deep-sleep entry point; powerOff is modeled
    // separately so scenarios can assert both shutdown surfaces.
    return "no";
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
  saveByte("auto_off", Settings::AUTO_OFF);
  saveByte("low_batt", Settings::LOW_BATT);
  saveBoolean("auto_off_charging", Settings::AUTO_OFF_CHARGING);
  saveBoolean("gps", Settings::GPS);
  saveBoolean("gps_nmea", Settings::GPS_NMEA);
  saveBoolean("fauxny", Settings::FAUXNY);
  saveBoolean("autoconnect", Settings::AUTOCONNECT);
  saveBoolean("reconnect", Settings::RECONNECT);
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
        std::exit(2);
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
        std::exit(2);
      }
      reading.charging = parseBool(value);
    }
    simulatedBattery = reading;
  }

  const auto uartMode = scenarioSettings.find("gps_uart_mode");
  if (uartMode != scenarioSettings.end()) {
    furble_sim_uart_set_mode(uartMode->second.c_str());
  }

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
  bool fuzz = false;
  uint64_t fuzzSeed = 1;
  uint32_t fuzzSteps = 500;
  bool fuzzVerbose = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--script" && i + 1 < argc) {
      script = argv[++i];
    } else if (argument == "--fuzz") {
      fuzz = true;
    } else if (argument == "--seed" && i + 1 < argc) {
      fuzz = true;
      fuzzSeed = std::stoull(argv[++i]);
    } else if (argument == "--fuzz-steps" && i + 1 < argc) {
      fuzz = true;
      fuzzSteps = parseUnsigned(argv[++i], "--fuzz-steps", std::numeric_limits<uint32_t>::max());
    } else if (argument == "--fuzz-verbose") {
      fuzzVerbose = true;
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
                   "[--rig-port PORT] [--ignore-uuid-mismatch] [--drop-notify] [--delay-ms MS] "
                   "[--fuzz] [--seed N] [--fuzz-steps N] [--fuzz-verbose]\n";
      std::exit(0);
    }
  }

  rigConfigure(rig, rigPort, ignoreUuidMismatch, dropNotify, delayMs);

  // Environment fallbacks let CI drive the fuzzer without argv edits.
  if (const char *env = std::getenv("FURBLE_FUZZ_SEED"); env != nullptr && env[0] != '\0') {
    fuzz = true;
    fuzzSeed = std::stoull(env);
  }
  if (const char *env = std::getenv("FURBLE_FUZZ_STEPS"); env != nullptr && env[0] != '\0') {
    fuzz = true;
    fuzzSteps = static_cast<uint32_t>(std::stoul(env));
  }

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
  }
}

void setBackTarget(Furble::UI *ui) {
  backTarget = ui;
}

void startProfiler(void) {
  profilerBegin(scenarioName.c_str());
}

void driverTick(void) {
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
        waiting = false;
        ++stepIndex;
      }
      break;

    case StepType::STALL:
      // Advance scenario time without returning to Platform::update. This
      // models a wedged UI task, so the virtual PM1 watchdog cannot be fed.
      advanceClock(step.milliseconds);
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
      if (!parseBatteryAction(step.name)) {
        scenarioUi->simScenarioAction(step.name.c_str());
      }
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
          std::_Exit(1);
        }
        // This is a host-side observation wait. It leaves the UI task blocked
        // while allowing background simulator tasks to process the state that
        // the preceding virtual-time steps made due.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      break;
    }

    case StepType::XASSERT:
    {
      // Expected-fail assertion: a known gap awaiting a separate product fix.
      // Never aborts the run, so CI stays green while the gap is documented.
      const std::string actual = queryValue(step.name);
      if (actual != step.expected) {
        std::cout << "XFAIL (WILL_FAIL): " << step.name << " expected '" << step.expected
                  << "' got '" << actual << "'\n";
      } else {
        std::cout << "XPASS (gap fixed, promote to assert): " << step.name << " = " << actual
                  << '\n';
      }
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
