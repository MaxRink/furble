// Host stand-in for the platform singleton. The engines call it only to arm and
// release the light-sleep wake source, so that is all this models.
#ifndef FURBLE_HOST_PLATFORM_H
#define FURBLE_HOST_PLATFORM_H

#include <cstdint>

namespace Furble {

class Platform {
 public:
  static Platform &getInstance(void);

  bool armMotionWake(void);
  void disarmMotionWake(void);
  bool motionWakeAsserted(void) const;
  void clearMotionWake(void);
  uint32_t motionWakeEdges(void) const;
  uint32_t getM5PM1RetryCount(void) const;

  bool wakeArmed = false;
  bool wakeAvailable = true;
  bool wakeAsserted = false;
  int wakeClears = 0;
  uint32_t edges = 0;
};

}  // namespace Furble

#endif
