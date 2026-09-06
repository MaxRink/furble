// Host companion shim. The console only asks the companion transport to reload
// its setting or password after a provisioning write, so the double counts
// those calls.
#ifndef FURBLE_COMPANION_H
#define FURBLE_COMPANION_H

namespace Furble {

class CompanionGatt {
 public:
  static CompanionGatt &getInstance(void);

  void reloadSetting(void);
  void reloadPassword(void);

 private:
  CompanionGatt() = default;
};

}  // namespace Furble

#endif
