#ifndef FURBLE_HEADLESS_PLATFORM_H
#define FURBLE_HEADLESS_PLATFORM_H

#include <cstdint>

namespace Furble {
class Platform {
 public:
  struct battery_caps_t {
    bool level = true;
    bool voltage = true;
    bool current = true;
    bool charging = true;
  };

  static Platform &getInstance(void);
  static void init(void);
  uint32_t tick(void);
  void update(void);
  void setCPUMaxFreq(uint8_t mhz);
  bool watchdogEnable(bool enable);
  void restart(void);
  void wakeM5PM1(void);
  battery_caps_t getBatteryCaps(void) const;
};
}  // namespace Furble

#endif
