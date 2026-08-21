#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

// Slim Platform double for the control end-to-end harness. The real Platform
// pulls in M5PM1 and the PMIC hardware; the real Control only calls tick() and
// watchdogFeed(), so the harness provides just those. Guard name matches the
// real header so a stray transitive include is a no-op.

#include <cstdint>

namespace Furble {

class Platform {
 public:
  static Platform &getInstance();

  Platform(Platform const &) = delete;
  Platform &operator=(Platform const &) = delete;

  uint32_t tick(void);
  void watchdogFeed(void);

 private:
  Platform() = default;
};

}  // namespace Furble

#endif
