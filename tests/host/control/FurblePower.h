// Host shim for include/FurblePower.h.
//
// The real header pulls in esp_pm, which is hardware only. Control acquires and
// releases the NO_LIGHT_SLEEP lock through Power::getInstance(); both are no-ops
// on the host. The include guard matches the real header exactly.
#ifndef FURBLE_POWER_H
#define FURBLE_POWER_H

#include <cstdint>

namespace Furble {
class Power {
 public:
  // Order and values mirror the real header so Control::POWER_LOCK_OWNER usage
  // and the NO_LIGHT_SLEEP selector compile identically.
  enum class LockType : uint8_t {
    NO_LIGHT_SLEEP = 0,
    CPU_FREQ_MAX = 1,
    APB_FREQ_MAX = 2,
  };

  static Power &getInstance();

  Power(Power const &) = delete;
  Power(Power &&) = delete;
  Power &operator=(Power const &) = delete;
  Power &operator=(Power &&) = delete;

  void acquire(LockType type, const char *owner) {
    (void)type;
    (void)owner;
  }
  void release(LockType type, const char *owner) {
    (void)type;
    (void)owner;
  }

 private:
  Power() {}
};
}  // namespace Furble

#endif
