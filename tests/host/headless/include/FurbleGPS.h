#ifndef FURBLE_HEADLESS_GPS_H
#define FURBLE_HEADLESS_GPS_H

namespace Furble {
class GPS {
 public:
  static GPS &getInstance(void);
  static void init(void);
  void update(void);
  void reloadSetting(void);
};
}  // namespace Furble

#endif
