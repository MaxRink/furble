#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "scenario_action.h"

namespace {

bool parses(const std::string &text, Furble::Sim::scenario_action_t *action) {
  std::string error;
  if (!Furble::Sim::parseScenarioAction(text, action, &error)) {
    std::cerr << "unexpected parse failure for '" << text << "': " << error << '\n';
    return false;
  }
  return true;
}

bool rejects(const std::string &text) {
  Furble::Sim::scenario_action_t action;
  std::string error;
  if (Furble::Sim::parseScenarioAction(text, &action, &error)) {
    std::cerr << "unexpected parse success for '" << text << "'\n";
    return false;
  }
  return !error.empty();
}

}  // namespace

int main() {
  using namespace Furble::Sim;
  scenario_action_t action;
  const std::vector<std::string> accepted = {
      "blind",
      "button-mode one-button",
      "battery 100 65535 -2147483648 off",
      "drop",
      "drop 0",
      "imu.accel -1 0.5 1",
      "imu.roll -180",
      "toggle gps",
      "nav diagnostics",
      "scroll top",
      "scroll bottom",
      "scroll next",
      "scroll -2147483647",
      "scroll 2147483647",
      "page main",
      "page menu",
      "page remote_disconnect",
      "expect applied page main",
      "expect no-effect select",
      "expect valid-no-effect companion-reject",
  };
  for (const std::string &text : accepted) {
    if (!parses(text, &action)) {
      return 1;
    }
  }

  const std::vector<std::string> rejected = {
      "imu.accel 0 nan 1",
      "imu.accel 0 1 2 trailing",
      "imu.roll inf",
      "imu.pitch 1e999",
      "drop 1 trailing",
      "drop -1",
      "drop +1",
      "battery -1 4000 0 off",
      "battery +1 4000 0 off",
      "battery 101 4000 0 off",
      "battery 80 65536 0 off",
      "battery 80 4000 0 maybe",
      "scroll -2147483648",
      "scroll 2147483648",
      "scroll 1 trailing",
      "page unknown",
      "nav unknown",
      "toggle unknown",
      "expect maybe page main",
      "expect unavailable",
  };
  for (const std::string &text : rejected) {
    if (!rejects(text)) {
      return 1;
    }
  }

  scenario_action_t forged;
  forged.kind = scenario_action_kind_t::DROP;
  forged.index = static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1U;
  if (validateScenarioAction(forged, nullptr)) {
    return 1;
  }
  forged = scenario_action_t {};
  forged.kind = scenario_action_kind_t::DROP;
  forged.name = "drop";
  if (validateScenarioAction(forged, nullptr)) {
    return 1;
  }
  forged = scenario_action_t {};
  forged.kind = scenario_action_kind_t::SCROLL;
  forged.name = "1";
  forged.integer = 2;
  if (validateScenarioAction(forged, nullptr)) {
    return 1;
  }
  forged = scenario_action_t {};
  forged.kind = scenario_action_kind_t::BATTERY;
  forged.batteryLevel = 101;
  if (validateScenarioAction(forged, nullptr)) {
    return 1;
  }
  forged = scenario_action_t {};
  forged.kind = scenario_action_kind_t::SIMPLE;
  forged.name = "connect";
  forged.index = 1;
  if (validateScenarioAction(forged, nullptr)) {
    return 1;
  }
  return 0;
}
