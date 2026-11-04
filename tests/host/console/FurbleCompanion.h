// Host companion shim. The console asks the companion transport to reload its
// setting or password after a write, so the double records the reload mode and
// reads the resulting enabled state back from the real Settings store.
#ifndef FURBLE_COMPANION_H
#define FURBLE_COMPANION_H

namespace Furble {

class CompanionGatt {
 public:
  static CompanionGatt &getInstance(void);

  void reloadSetting(bool pairingWindow = false);
  void disableNow(void);
  bool isEnabled(void) const;
  void reloadPassword(void);

 private:
  CompanionGatt() = default;
};

}  // namespace Furble

#endif
