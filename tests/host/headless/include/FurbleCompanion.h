#ifndef FURBLE_HEADLESS_COMPANION_H
#define FURBLE_HEADLESS_COMPANION_H

namespace Furble {
class CompanionGatt {
 public:
  static CompanionGatt &getInstance(void);
  void init(void);
};

using Companion = CompanionGatt;
}  // namespace Furble

#endif
