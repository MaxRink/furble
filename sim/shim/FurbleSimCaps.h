#ifndef FURBLE_SIM_CAPS_H
#define FURBLE_SIM_CAPS_H

#include <cstdlib>
#include <cstring>

// Optional-hardware presence switches for the host simulator. The modeled
// board has no IR LED, feedback output or SD card, so the Infrared, Feedback
// and Storage submenus are hidden by default and the scripted key-count menu
// routes keep their positions. A capture run that wants those pages to render
// sets the matching environment variable before launching the sim, which flips
// the corresponding fake capability on. This is sim-only. It lives entirely in
// sim/shim and never touches the on-device hardware detection.
//
//   FURBLE_SIM_IR        report an IR LED present
//   FURBLE_SIM_FEEDBACK  report a full feedback output set (sound, light, ...)
//   FURBLE_SIM_SD        report a mounted SD card
namespace Furble::Sim {

inline bool capEnabled(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

}  // namespace Furble::Sim

#endif
