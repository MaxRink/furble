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

  uint32_t tick(void) const;
  void watchdogFeed(void);

 private:
  Platform() = default;
};

}  // namespace Furble

#endif
