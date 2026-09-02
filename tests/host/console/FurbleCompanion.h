// Host companion shim. The console only asks the companion transport to reload
// its setting after a provisioning write, so the double counts that call.
#ifndef FURBLE_COMPANION_H
#define FURBLE_COMPANION_H

namespace Furble {

class CompanionGatt {
 public:
  static CompanionGatt &getInstance(void);

  void reloadSetting(void);

 private:
  CompanionGatt() = default;
};

}  // namespace Furble

#endif
