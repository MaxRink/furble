// Host SD shim. The console asks only whether the board has a card slot, which
// gates the sd_gpx setting write.
#ifndef FURBLE_SD_H
#define FURBLE_SD_H

namespace Furble {

class SD {
 public:
  static SD &getInstance(void);

  bool isSupported(void) const;

 private:
  SD() = default;
};

}  // namespace Furble

#endif
