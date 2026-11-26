#ifndef FURBLE_PLATFORM_SHIM_SD_H
#define FURBLE_PLATFORM_SHIM_SD_H

namespace Furble {
class SD {
 public:
  static SD &getInstance(void);
  bool powerOff(void);
};
}  // namespace Furble

#endif
