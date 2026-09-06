// Host stand-in for the platform singleton. The engines call it only to arm and
// release the light-sleep wake source, so that is all this models.
#ifndef FURBLE_HOST_PLATFORM_H
#define FURBLE_HOST_PLATFORM_H

namespace Furble {

class Platform {
 public:
  static Platform &getInstance(void);

  bool armMotionWake(void);
  void disarmMotionWake(void);

  bool wakeArmed = false;
  bool wakeAvailable = true;
};

}  // namespace Furble

#endif
