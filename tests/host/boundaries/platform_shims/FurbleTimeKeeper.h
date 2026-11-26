#ifndef FURBLE_PLATFORM_SHIM_TIME_KEEPER_H
#define FURBLE_PLATFORM_SHIM_TIME_KEEPER_H

namespace Furble {
class TimeKeeper {
 public:
  static TimeKeeper &getInstance(void);
  void flush(void);
};
}  // namespace Furble

#endif
