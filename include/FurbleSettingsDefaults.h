#ifndef FURBLE_SETTINGS_DEFAULTS_H
#define FURBLE_SETTINGS_DEFAULTS_H

#include <cstdint>

namespace Furble {
namespace SettingsDefaults {

// Fresh-device defaults. Kept dependency-free so a host test can compile the
// board-conditional policy both with and without FURBLE_M5STICKS3.
constexpr uint8_t INACTIVITY = 2;  // 60 secs

constexpr uint8_t displayOff() {
  // Every board now starts at Off. The IP5306 Core used to seed Dim because a
  // panel-off state falls under the 45 mA light-load threshold and the boost
  // cut looked like a power off. Platform::keepIp5306BoostOn() holds SYS_CTL0
  // bit 1 for the whole session, so that cut cannot fire and the workaround is
  // no longer needed (plan 354).
  return 1;  // Off
}

constexpr bool sleepConn() {
#if defined(FURBLE_M5STICKS3)
  return true;
#else
  return false;
#endif
}

}  // namespace SettingsDefaults
}  // namespace Furble

#endif
