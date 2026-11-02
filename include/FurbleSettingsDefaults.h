#ifndef FURBLE_SETTINGS_DEFAULTS_H
#define FURBLE_SETTINGS_DEFAULTS_H

#include <cstdint>

namespace Furble {
namespace SettingsDefaults {

// Fresh-device defaults. Kept dependency-free so a host test can compile the
// board-conditional policy both with and without FURBLE_M5STICKS3.
constexpr uint8_t INACTIVITY = 2;   // 60 secs
constexpr uint8_t DISPLAY_OFF = 1;  // Off

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
