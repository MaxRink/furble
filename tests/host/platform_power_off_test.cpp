// Platform power-off refusal.
//
// Platform::powerOff() asks PlatformPolicy::powerOffAction() first. A board row
// without a power-off path has no PMIC, no battery and no switchable rail, so
// M5Unified's Power_Class::powerOff() arms no wake source and ends at
// esp_deep_sleep_start(), leaving the node asleep until a power cycle while the
// call reports success. The production path logs and returns false there; this
// test pins the decision for every board row.
//
// The decision reads `powerOffPath`, not `canPowerOff`. `canPowerOff` is the UI
// policy gate, and it is already false on the Core, which M5Unified does power
// down through the IP5306 boost keep-off. Only the Waveshare ESP32-S3-ETH
// reports no path. See plans/349-waveshare-board-row.md.

#include <iostream>
#include <string>

#include "FurbleBoard.h"
#include "FurblePlatformPolicy.h"

using Furble::Board::Profile;
using Furble::PlatformPolicy::PowerOffAction;
using Furble::PlatformPolicy::powerOffAction;

namespace {

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

struct Expected {
  m5::board_t board;
  const char *name;
  PowerOffAction action;
};

// clang-format off
const Expected EXPECTED[] = {
  {m5::board_t::board_M5StickC,      "StickC",    PowerOffAction::CUT},
  {m5::board_t::board_M5StickCPlus,  "StickC+",   PowerOffAction::CUT},
  {m5::board_t::board_M5StickCPlus2, "StickC+2",  PowerOffAction::CUT},
  {m5::board_t::board_M5StickS3,     "StickS3",   PowerOffAction::CUT},
  // No automatic policies on the IP5306 Core, but it still powers down.
  {m5::board_t::board_M5Stack,       "Core",      PowerOffAction::CUT},
  {m5::board_t::board_M5StackCore2,  "Core2",     PowerOffAction::CUT},
  {m5::board_t::board_M5Tough,       "Tough",     PowerOffAction::CUT},
  // Mains or PoE, no PMIC and no switchable rail: nothing to switch.
  {m5::board_t::board_M5StampS3,     "Waveshare", PowerOffAction::REFUSE},
  // A board furble has no row for keeps the generic attempt.
  {m5::board_t::board_unknown,       "M5",        PowerOffAction::CUT},
  {m5::board_t::board_M5StackCoreS3, "M5",        PowerOffAction::CUT},
};
// clang-format on

}  // namespace

int main() {
  // The decision follows one column and nothing else.
  check(powerOffAction(true) == PowerOffAction::CUT, "a power-off path is attempted");
  check(powerOffAction(false) == PowerOffAction::REFUSE, "a missing power-off path is refused");

  for (const auto &e : EXPECTED) {
    const Profile &p = Furble::Board::profileFor(e.board);
    const std::string n = e.name;
    check(powerOffAction(p.powerOffPath) == e.action, n + " power off action");
    // The row and the action must agree, so a future row cannot report a
    // power-off path and still be refused, or the reverse.
    check((e.action == PowerOffAction::REFUSE) == !p.powerOffPath, n + " action matches the row");
  }

  // The Core keeps a path even though it offers no automatic policies. The two
  // columns are separate on purpose.
  const Profile &core = Furble::Board::profileFor(m5::board_t::board_M5Stack);
  check(!core.canPowerOff, "the Core offers no automatic power policies");
  check(core.powerOffPath, "the Core still has a power-off path");

  // The Waveshare refusal is the only one, so the generic row keeps working.
  size_t refusals = 0;
  for (const auto &e : EXPECTED) {
    if (e.action == PowerOffAction::REFUSE) {
      refusals++;
    }
  }
  check(refusals == 1, "only the Waveshare row refuses a power off");

  if (g_Failures == 0) {
    std::cout << "platform-power-off: PASS\n";
    return 0;
  }
  return 1;
}
