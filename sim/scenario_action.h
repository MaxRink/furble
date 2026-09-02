#ifndef FURBLE_SIM_SCENARIO_ACTION_H
#define FURBLE_SIM_SCENARIO_ACTION_H

#include <cstdint>
#include <string>

namespace Furble::Sim {

enum class scenario_action_kind_t {
  SIMPLE,
  BUTTON_MODE,
  BATTERY,
  DROP,
  IMU_VECTOR,
  IMU_ANGLE,
  TOGGLE,
  NAV,
  SCROLL,
  PAGE,
  CONSOLE,
  INVALID,
};

enum class scenario_action_expectation_t {
  APPLIED,
  VALID_NO_EFFECT,
  UNAVAILABLE,
};

// Parsed once at the scenario boundary. Runtime dispatch consumes this typed
// value so whitespace and numeric spelling cannot change between validation
// and execution.
struct scenario_action_t {
  scenario_action_kind_t kind = scenario_action_kind_t::INVALID;
  scenario_action_expectation_t expectation = scenario_action_expectation_t::APPLIED;
  std::string name;
  std::string mode;
  uint32_t index = 0;
  int32_t integer = 0;
  float values[3] = {0.0F, 0.0F, 0.0F};
  uint8_t batteryLevel = 0;
  uint16_t batteryVoltage = 0;
  int32_t batteryCurrent = 0;
  bool batteryCharging = false;
};

bool parseScenarioAction(const std::string &text, scenario_action_t *action, std::string *error);

// Validate both parser output and values supplied by typed callers. The
// simulator boundary is fail closed because scenario_action_t is intentionally
// public for host tests and can therefore be forged by a caller.
bool validateScenarioAction(const scenario_action_t &action, std::string *error);

}  // namespace Furble::Sim

#endif
