// Fresh-device defaults, compiled without FURBLE_M5STICKS3.
//
// The NVS round-trip test builds only as the StickS3, so it cannot catch a
// regression in the non-S3 sleep-while-connected default. This target pins the
// other branch of the board-conditional policy.

#include <cstdint>
#include <cstdio>

#include "FurbleSettingsDefaults.h"

int main() {
  bool ok = true;
  if (Furble::SettingsDefaults::INACTIVITY != 2) {
    std::fprintf(stderr, "INACTIVITY default is not 60 secs\n");
    ok = false;
  }
#if defined(FURBLE_M5STACK_CORE)
  if (Furble::SettingsDefaults::displayOff() != 0) {
    std::fprintf(stderr, "Core Basic DISPLAY_OFF default is not Dim\n");
    ok = false;
  }
#else
  if (Furble::SettingsDefaults::displayOff() != 1) {
    std::fprintf(stderr, "DISPLAY_OFF default is not Off\n");
    ok = false;
  }
#endif
#if defined(FURBLE_M5STICKS3)
  if (!Furble::SettingsDefaults::sleepConn()) {
    std::fprintf(stderr, "StickS3 sleep-while-connected default is not on\n");
    ok = false;
  }
#else
  if (Furble::SettingsDefaults::sleepConn()) {
    std::fprintf(stderr, "non-StickS3 sleep-while-connected default is not off\n");
    ok = false;
  }
#endif
  if (!ok) {
    return 1;
  }
  std::printf("settings default policy: PASS\n");
  return 0;
}
