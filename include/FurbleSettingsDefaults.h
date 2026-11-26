#ifndef FURBLE_SETTINGS_DEFAULTS_H
#define FURBLE_SETTINGS_DEFAULTS_H

#include <cstdint>

namespace Furble {
namespace SettingsDefaults {

// Fresh-device defaults. Kept dependency-free so a host test can compile the
// board-conditional policy both with and without FURBLE_M5STICKS3.
constexpr uint8_t INACTIVITY = 2;  // 60 secs

constexpr uint8_t displayOff() {
#if defined(FURBLE_M5STACK_CORE)
  // The Core Basic's IP5306 boost cuts the board below 45 mA after 64 s.
  // M5Unified 0.2.20 writes SYS_CTL2 (0x02) = 0x6C at M5.begin(); bits 3:2 =
  // 11 select the 64 s light-load timeout, not the 32 s power-on default.
  // Keep the panel dim rather than Off so a fresh Core is not powered down
  // by its own low-power default.
  return 0;  // Dim
#else
  return 1;  // Off
#endif
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
