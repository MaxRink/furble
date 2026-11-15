#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <cstdint>

namespace Furble {

class Platform {
 public:
  static Platform &getInstance();

  Platform(Platform const &) = delete;
  Platform(Platform &&) = delete;
  Platform &operator=(Platform const &) = delete;
  Platform &operator=(Platform &&) = delete;

  /** Mirrors the production companion battery status record. */
  typedef struct {
    int32_t level;
    int16_t voltage;
    int32_t current;
    int16_t vbus;
    bool charging;
  } battery_status_t;

  uint32_t tick(void) const;
  void watchdogFeed(void);
  battery_status_t getBatteryStatus(void);

 private:
  Platform() = default;
};

}  // namespace Furble

#endif
