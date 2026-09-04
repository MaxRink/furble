#include "scenario_action.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <vector>

namespace Furble::Sim {
namespace {

std::vector<std::string> splitWords(const std::string &text) {
  std::istringstream input(text);
  std::vector<std::string> words;
  std::string word;
  while (input >> word) {
    words.push_back(word);
  }
  return words;
}

bool oneOf(const std::string &value, std::initializer_list<const char *> values) {
  for (const char *candidate : values) {
    if (value == candidate) {
      return true;
    }
  }
  return false;
}

bool parseUnsigned(const std::string &text, uint64_t maximum, uint64_t *value) {
  if (text.empty() || text[0] == '+' || text[0] == '-') {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed > maximum) {
    return false;
  }
  *value = static_cast<uint64_t>(parsed);
  return true;
}

bool parseSigned(const std::string &text, int64_t minimum, int64_t maximum, int64_t *value) {
  errno = 0;
  char *end = nullptr;
  const long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed < minimum
      || parsed > maximum) {
    return false;
  }
  *value = static_cast<int64_t>(parsed);
  return true;
}

bool parseFloat(const std::string &text, float *value) {
  errno = 0;
  char *end = nullptr;
  const float parsed = std::strtof(text.c_str(), &end);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  *value = parsed;
  return true;
}

bool fail(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool allZero(const scenario_action_t &action) {
  return action.index == 0 && action.integer == 0 && action.batteryLevel == 0
         && action.batteryVoltage == 0 && action.batteryCurrent == 0 && !action.batteryCharging
         && action.values[0] == 0.0F && action.values[1] == 0.0F && action.values[2] == 0.0F;
}

bool allZeroExceptValues(const scenario_action_t &action) {
  return action.index == 0 && action.integer == 0 && action.batteryLevel == 0
         && action.batteryVoltage == 0 && action.batteryCurrent == 0 && !action.batteryCharging
         && action.mode.empty();
}

bool known(const std::string &value, std::initializer_list<const char *> values) {
  return oneOf(value, values);
}

}  // namespace

bool validateScenarioAction(const scenario_action_t &action, std::string *error) {
  if (action.expectation != scenario_action_expectation_t::APPLIED
      && action.expectation != scenario_action_expectation_t::VALID_NO_EFFECT
      && action.expectation != scenario_action_expectation_t::UNAVAILABLE) {
    return fail(error, "invalid action expectation");
  }

  switch (action.kind) {
    case scenario_action_kind_t::SIMPLE:
      if (!action.mode.empty() || !allZero(action)
          || !known(action.name, {"blind",
                                  "blind-shutter",
                                  "indicator-click-focus",
                                  "focus-lock",
                                  "connect",
                                  "connect-two",
                                  "disconnect",
                                  "ble-kill",
                                  "ble-standby",
                                  "ble-connect-fail",
                                  "ble-connect-ok",
                                  "ble-withhold-registration",
                                  "ble-allow-registration",
                                  "cancel",
                                  "imu.enable",
                                  "imu.disable",
                                  "imu.accel.fail",
                                  "imu.accel.recover",
                                  "imu.gyro.fail",
                                  "imu.gyro.recover",
                                  "invalidate.reset",
                                  "shutter",
                                  "main-press-hold",
                                  "main-double-click",
                                  "main-click-hold",
                                  "select",
                                  "intervalometer",
                                  "bulb-start",
                                  "bulb-stop",
                                  "stop",
                                  "preset-step-up",
                                  "preset-step-down",
                                  "companion-pair-request",
                                  "companion-accept",
                                  "companion-reject"})) {
        return fail(error, "noncanonical simple action");
      }
      return true;
    case scenario_action_kind_t::BUTTON_MODE:
      if (!action.name.empty() || !allZero(action)
          || !known(action.mode, {"one-button", "two-button"})) {
        return fail(error, "noncanonical button mode action");
      }
      return true;
    case scenario_action_kind_t::BATTERY:
      if (!action.name.empty() || !action.mode.empty() || action.index != 0 || action.integer != 0
          || action.values[0] != 0.0F || action.values[1] != 0.0F || action.values[2] != 0.0F
          || action.batteryLevel > 100) {
        return fail(error, "noncanonical battery action");
      }
      return true;
    case scenario_action_kind_t::DROP:
      if (!action.name.empty() || !action.mode.empty() || action.integer != 0
          || action.batteryLevel != 0 || action.batteryVoltage != 0 || action.batteryCurrent != 0
          || action.batteryCharging || action.values[0] != 0.0F || action.values[1] != 0.0F
          || action.values[2] != 0.0F
          || (action.index != std::numeric_limits<uint32_t>::max()
              && action.index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))) {
        return fail(error, "noncanonical drop action");
      }
      return true;
    case scenario_action_kind_t::SECURE_STALL:
      if (!action.name.empty() || !action.mode.empty() || action.integer != 0
          || action.batteryLevel != 0 || action.batteryVoltage != 0 || action.batteryCurrent != 0
          || action.batteryCharging || action.values[0] != 0.0F || action.values[1] != 0.0F
          || action.values[2] != 0.0F || action.index > SECURE_STALL_MAX_MS) {
        return fail(error, "noncanonical secure stall action");
      }
      return true;
    case scenario_action_kind_t::IMU_VECTOR:
      if (!action.mode.empty() || !allZeroExceptValues(action)
          || !known(action.name, {"imu.accel", "imu.gyro"}) || !std::isfinite(action.values[0])
          || !std::isfinite(action.values[1]) || !std::isfinite(action.values[2])) {
        return fail(error, "noncanonical IMU vector action");
      }
      return true;
    case scenario_action_kind_t::IMU_ANGLE:
      if (!action.mode.empty() || action.index != 0 || action.integer != 0
          || action.batteryLevel != 0 || action.batteryVoltage != 0 || action.batteryCurrent != 0
          || action.batteryCharging || action.values[1] != 0.0F || action.values[2] != 0.0F
          || !known(action.name, {"imu.roll", "imu.pitch"}) || !std::isfinite(action.values[0])) {
        return fail(error, "noncanonical IMU angle action");
      }
      return true;
    case scenario_action_kind_t::TOGGLE:
      if (!action.mode.empty() || !allZero(action)
          || !known(action.name, {"gps", "gps_nmea", "autoconnect", "reconnect", "multiconnect",
                                  "companion", "watchdog", "ir", "show_title", "tx_adaptive",
                                  "conn_saver", "preset_picker", "recon_backoff"})) {
        return fail(error, "noncanonical toggle action");
      }
      return true;
    case scenario_action_kind_t::NAV:
      if (!action.mode.empty() || !allZero(action)
          || !known(action.name, {"connect",
                                  "scan",
                                  "delete",
                                  "power_off",
                                  "bulb_duration",
                                  "bulb",
                                  "settings",
                                  "display",
                                  "features",
                                  "sensors",
                                  "infrared",
                                  "gps_rate",
                                  "gps_sentences",
                                  "gps_constellation",
                                  "gps_power",
                                  "gps_assist",
                                  "gps",
                                  "gps_data",
                                  "nmea",
                                  "timer",
                                  "theme",
                                  "text_size",
                                  "bluetooth",
                                  "tx_power",
                                  "about",
                                  "power",
                                  "feedback",
                                  "feedback_events",
                                  "feedback_volume",
                                  "diagnostics",
                                  "device_info",
                                  "power_state",
                                  "ble",
                                  "interval_count",
                                  "interval_delay",
                                  "interval_shutter",
                                  "interval_wait",
                                  "battery",
                                  "storage",
                                  "imu",
                                  "level",
                                  "level_main"})) {
        return fail(error, "noncanonical navigation action");
      }
      return true;
    case scenario_action_kind_t::SCROLL:
    {
      if (!action.mode.empty() || action.index != 0 || action.batteryLevel != 0
          || action.batteryVoltage != 0 || action.batteryCurrent != 0 || action.batteryCharging
          || action.values[0] != 0.0F || action.values[1] != 0.0F || action.values[2] != 0.0F) {
        return fail(error, "noncanonical scroll action");
      }
      if (known(action.name, {"top", "bottom", "next"})) {
        return action.integer == 0 ? true : fail(error, "symbolic scroll has a nonzero value");
      }
      int64_t pixels = 0;
      if (!parseSigned(action.name, -static_cast<int64_t>(std::numeric_limits<int32_t>::max()),
                       std::numeric_limits<int32_t>::max(), &pixels)
          || action.integer != pixels) {
        return fail(error, "noncanonical scroll pixel count");
      }
      return true;
    }
    case scenario_action_kind_t::PAGE:
      if (!action.mode.empty() || !allZero(action)
          || !known(action.name, {"main",
                                  "menu",
                                  "connect",
                                  "scan",
                                  "delete",
                                  "power_off",
                                  "connected",
                                  "ir",
                                  "shutter",
                                  "bulb",
                                  "bulb_duration",
                                  "bulb_run",
                                  "cameras",
                                  "remote_timer",
                                  "remote_gps",
                                  "remote_disconnect",
                                  "timer",
                                  "timer_run",
                                  "settings",
                                  "display",
                                  "features",
                                  "sensors",
                                  "infrared",
                                  "gps_rate",
                                  "gps_sentences",
                                  "gps_constellation",
                                  "gps_power",
                                  "gps_assist",
                                  "gps",
                                  "gps_data",
                                  "nmea",
                                  "theme",
                                  "text_size",
                                  "bluetooth",
                                  "tx_power",
                                  "about",
                                  "power",
                                  "feedback",
                                  "feedback_events",
                                  "feedback_volume",
                                  "storage",
                                  "diagnostics",
                                  "device_info",
                                  "battery",
                                  "power_state",
                                  "ble",
                                  "interval_count",
                                  "interval_delay",
                                  "interval_shutter",
                                  "interval_wait"})) {
        return fail(error, "noncanonical page action");
      }
      return true;
    case scenario_action_kind_t::INVALID:
      return fail(error, "invalid action kind");
  }
  return fail(error, "unknown action kind");
}

bool parseScenarioAction(const std::string &text, scenario_action_t *action, std::string *error) {
  if (action == nullptr) {
    return fail(error, "missing action output");
  }
  *action = scenario_action_t {};
  std::vector<std::string> args = splitWords(text);
  if (args.empty()) {
    return fail(error, "an action is required");
  }

  if (args[0] == "expect") {
    if (args.size() < 3) {
      return fail(error, "expect requires an outcome and an action");
    }
    if (args[1] == "applied") {
      action->expectation = scenario_action_expectation_t::APPLIED;
    } else if (args[1] == "no-effect" || args[1] == "valid-no-effect") {
      action->expectation = scenario_action_expectation_t::VALID_NO_EFFECT;
    } else if (args[1] == "unavailable") {
      action->expectation = scenario_action_expectation_t::UNAVAILABLE;
    } else {
      return fail(error, "expect outcome must be applied, no-effect, or unavailable");
    }
    args.erase(args.begin(), args.begin() + 2);
  }

  const auto accept = [&action, &error]() { return validateScenarioAction(*action, error); };

  const auto exact = [&args](std::initializer_list<const char *> expected) {
    if (args.size() != expected.size()) {
      return false;
    }
    size_t index = 0;
    for (const char *word : expected) {
      if (args[index++] != word) {
        return false;
      }
    }
    return true;
  };

  const std::initializer_list<const char *> simple = {
      "blind",
      "blind-shutter",
      "indicator-click-focus",
      "focus-lock",
      "connect",
      "connect-two",
      "disconnect",
      "ble-kill",
      "ble-standby",
      "ble-connect-fail",
      "ble-connect-ok",
      "ble-withhold-registration",
      "ble-allow-registration",
      "cancel",
      "imu.enable",
      "imu.disable",
      "imu.accel.fail",
      "imu.accel.recover",
      "imu.gyro.fail",
      "imu.gyro.recover",
      "invalidate.reset",
      "shutter",
      "main-press-hold",
      "main-double-click",
      "main-click-hold",
      "select",
      "intervalometer",
      "bulb-start",
      "bulb-stop",
      "stop",
      "preset-step-up",
      "preset-step-down",
      "companion-pair-request",
      "companion-accept",
      "companion-reject",
  };
  for (const char *command : simple) {
    if (exact({command})) {
      action->kind = scenario_action_kind_t::SIMPLE;
      action->name = command;
      return accept();
    }
  }

  if (args[0] == "button-mode") {
    if (args.size() != 2 || !oneOf(args[1], {"one-button", "two-button"})) {
      return fail(error, "button-mode requires one-button or two-button");
    }
    action->kind = scenario_action_kind_t::BUTTON_MODE;
    action->mode = args[1];
    return accept();
  }

  if (args[0] == "battery") {
    if (args.size() != 5) {
      return fail(error, "battery requires level voltage current charging");
    }
    uint64_t level = 0;
    uint64_t voltage = 0;
    int64_t current = 0;
    if (!parseUnsigned(args[1], 100, &level) || !parseUnsigned(args[2], 65535, &voltage)
        || !parseSigned(args[3], std::numeric_limits<int32_t>::min(),
                        std::numeric_limits<int32_t>::max(), &current)
        || !oneOf(args[4], {"0", "1", "true", "false", "yes", "no", "on", "off"})) {
      return fail(error, "invalid battery value");
    }
    action->kind = scenario_action_kind_t::BATTERY;
    action->batteryLevel = static_cast<uint8_t>(level);
    action->batteryVoltage = static_cast<uint16_t>(voltage);
    action->batteryCurrent = static_cast<int32_t>(current);
    action->batteryCharging = oneOf(args[4], {"1", "true", "yes", "on"});
    return accept();
  }

  if (args[0] == "ble-secure-stall") {
    if (args.size() != 2) {
      return fail(error, "ble-secure-stall requires a duration in milliseconds");
    }
    uint64_t stall = 0;
    if (!parseUnsigned(args[1], SECURE_STALL_MAX_MS, &stall)) {
      return fail(error, "ble-secure-stall duration is out of range");
    }
    action->kind = scenario_action_kind_t::SECURE_STALL;
    action->index = static_cast<uint32_t>(stall);
    return accept();
  }

  if (args[0] == "drop") {
    if (args.size() > 2) {
      return fail(error, "drop accepts an optional target index");
    }
    action->kind = scenario_action_kind_t::DROP;
    action->index = std::numeric_limits<uint32_t>::max();
    if (args.size() == 2) {
      uint64_t index = 0;
      if (!parseUnsigned(args[1], static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
                         &index)) {
        return fail(error, "drop target index is out of range");
      }
      action->index = static_cast<uint32_t>(index);
    }
    return accept();
  }

  if (args[0] == "imu.accel" || args[0] == "imu.gyro") {
    if (args.size() != 4) {
      return fail(error, "imu vector requires exactly three finite numbers");
    }
    for (size_t index = 0; index < 3; index++) {
      if (!parseFloat(args[index + 1], &action->values[index])) {
        return fail(error, "imu values must be finite numbers");
      }
    }
    action->kind = scenario_action_kind_t::IMU_VECTOR;
    action->name = args[0];
    return accept();
  }
  if (args[0] == "imu.roll" || args[0] == "imu.pitch") {
    if (args.size() != 2 || !parseFloat(args[1], &action->values[0])) {
      return fail(error, "imu angle requires one finite number");
    }
    action->kind = scenario_action_kind_t::IMU_ANGLE;
    action->name = args[0];
    return accept();
  }

  if (args[0] == "toggle") {
    if (args.size() != 2
        || !oneOf(args[1], {"gps", "gps_nmea", "autoconnect", "reconnect", "multiconnect",
                            "companion", "watchdog", "ir", "show_title", "tx_adaptive",
                            "conn_saver", "preset_picker", "recon_backoff"})) {
      return fail(error, "toggle requires a known setting name");
    }
    action->kind = scenario_action_kind_t::TOGGLE;
    action->name = args[1];
    return accept();
  }

  const std::initializer_list<const char *> pages = {
      "connect",
      "scan",
      "delete",
      "power_off",
      "bulb_duration",
      "bulb",
      "settings",
      "display",
      "features",
      "sensors",
      "infrared",
      "gps_rate",
      "gps_sentences",
      "gps_constellation",
      "gps_power",
      "gps_assist",
      "gps",
      "gps_data",
      "nmea",
      "timer",
      "theme",
      "text_size",
      "bluetooth",
      "tx_power",
      "about",
      "power",
      "feedback",
      "feedback_events",
      "feedback_volume",
      "diagnostics",
      "device_info",
      "power_state",
      "ble",
      "interval_count",
      "interval_delay",
      "interval_shutter",
      "interval_wait",
      "battery",
      "storage",
      "imu",
      "level",
      "level_main",
  };
  const auto pageKnown = [&pages](const std::string &value) {
    for (const char *page : pages) {
      if (value == page) {
        return true;
      }
    }
    return false;
  };
  if (args[0] == "nav") {
    if (args.size() != 2 || !pageKnown(args[1])) {
      return fail(error, "nav requires a known page name");
    }
    action->kind = scenario_action_kind_t::NAV;
    action->name = args[1];
    return accept();
  }
  if (args[0] == "scroll") {
    if (args.size() != 2) {
      return fail(error, "scroll requires top, bottom, next, or a signed pixel count");
    }
    action->kind = scenario_action_kind_t::SCROLL;
    action->name = args[1];
    if (oneOf(args[1], {"top", "bottom", "next"})) {
      return accept();
    }
    int64_t pixels = 0;
    if (!parseSigned(args[1], -static_cast<int64_t>(std::numeric_limits<int32_t>::max()),
                     std::numeric_limits<int32_t>::max(), &pixels)) {
      return fail(error, "scroll pixel count must be between -2147483647 and 2147483647");
    }
    action->integer = static_cast<int32_t>(pixels);
    return accept();
  }
  if (args[0] == "page") {
    const std::initializer_list<const char *> pageActions = {
        "main",
        "menu",
        "connect",
        "scan",
        "delete",
        "power_off",
        "connected",
        "ir",
        "shutter",
        "bulb",
        "bulb_duration",
        "bulb_run",
        "cameras",
        "remote_timer",
        "remote_gps",
        "remote_disconnect",
        "timer",
        "timer_run",
        "settings",
        "display",
        "features",
        "sensors",
        "infrared",
        "gps_rate",
        "gps_sentences",
        "gps_constellation",
        "gps_power",
        "gps_assist",
        "gps",
        "gps_data",
        "nmea",
        "theme",
        "text_size",
        "bluetooth",
        "tx_power",
        "about",
        "power",
        "feedback",
        "feedback_events",
        "feedback_volume",
        "storage",
        "diagnostics",
        "device_info",
        "battery",
        "power_state",
        "ble",
        "interval_count",
        "interval_delay",
        "interval_shutter",
        "interval_wait",
    };
    bool known = args.size() == 2;
    if (known) {
      known = false;
      for (const char *page : pageActions) {
        if (args[1] == page) {
          known = true;
          break;
        }
      }
    }
    if (!known) {
      return fail(error, "page requires a known page name");
    }
    action->kind = scenario_action_kind_t::PAGE;
    action->name = args[1];
    return accept();
  }

  return fail(error, "unknown action or argument list");
}

}  // namespace Furble::Sim
