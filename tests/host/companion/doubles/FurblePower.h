#ifndef FURBLE_POWER_H
#define FURBLE_POWER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Furble {

class Power {
 public:
  enum class LockType : uint8_t {
    NO_LIGHT_SLEEP = 0,
    CPU_FREQ_MAX = 1,
    APB_FREQ_MAX = 2,
  };

  static constexpr size_t LOCK_COUNT = 3;

  static Power &getInstance();

  Power(Power const &) = delete;
  Power(Power &&) = delete;
  Power &operator=(Power const &) = delete;
  Power &operator=(Power &&) = delete;

  void acquire(LockType type, const char *owner);
  void release(LockType type, const char *owner);
  uint32_t getCount(LockType type) const;

 private:
  Power() = default;
  std::array<std::atomic<uint32_t>, LOCK_COUNT> m_Counts = {};
};

}  // namespace Furble

#endif
