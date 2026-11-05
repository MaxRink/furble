#ifndef FURBLE_HEADLESS_BOOT_SCREEN_H
#define FURBLE_HEADLESS_BOOT_SCREEN_H

#include <cstdint>

namespace Furble {
class BootScreen {
 public:
  static void begin(uint8_t totalStages);
  static void step(const char *label);
  static void finish(void);
  static bool wasShown(void);
  static uint8_t stepsShown(void);
};
}  // namespace Furble

#endif
