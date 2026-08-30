#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

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
  if (!parses("  page   main  ", &action)
      || action.kind != scenario_action_kind_t::PAGE || action.name != "main"
      || !parses("page menu", &action)
      || action.kind != scenario_action_kind_t::PAGE || action.name != "menu"
      || !parses("scroll -2147483647", &action)
      || action.integer != -std::numeric_limits<int32_t>::max()
      || !parses("scroll 2147483647", &action)
      || action.integer != std::numeric_limits<int32_t>::max()
      || !parses("battery 100 65535 -2147483648 off", &action)
      || action.batteryCurrent != std::numeric_limits<int32_t>::min()
      || !rejects("imu.accel 0 nan 1") || !rejects("drop 1 trailing")
      || !rejects("scroll -2147483648") || !rejects("scroll 2147483648")
      || !rejects("page unknown")) {
    return 1;
  }
  return 0;
}
