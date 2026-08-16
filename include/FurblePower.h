#ifndef FURBLE_POWER_H
#define FURBLE_POWER_H

#include <array>
#include <atomic>

#include <esp_err.h>
#include <esp_pm.h>

namespace Furble {
class Power {
 public:
  /**
   * Power management locks held by furble.
   *
   * The order must match m_Locks.
   */
  enum class LockType : uint8_t {
    NO_LIGHT_SLEEP = 0,
    CPU_FREQ_MAX = 1,
    APB_FREQ_MAX = 2,
  };

  /**
   * Hold a power management lock for the lifetime of the object.
   */
  class Lock {
   public:
    Lock(LockType type, const char *owner);
    ~Lock();

    Lock(Lock const &) = delete;
    Lock(Lock &&) = delete;
    Lock &operator=(Lock const &) = delete;
    Lock &operator=(Lock &&) = delete;

   private:
    const LockType m_Type;
    const char *const m_Owner;
  };

  /** Minimum CPU frequency in MHz, the floor esp_pm scales down to. */
  static constexpr const uint8_t CPU_MIN_FREQ_MHZ = 40;

  static Power &getInstance();

  Power(Power const &) = delete;
  Power(Power &&) = delete;
  Power &operator=(Power const &) = delete;
  Power &operator=(Power &&) = delete;

  static void init(void);

  /**
   * Apply the power management configuration.
   *
   * Automatic light sleep is always enabled, the NO_LIGHT_SLEEP lock is what
   * inhibits it.
   */
  esp_err_t configure(uint8_t max_freq_mhz);

  /**
   * Acquire a lock, inhibiting the matching power saving.
   *
   * The locks are counted, every acquire needs a matching release. The owner
   * string is for logging only and must be a static literal.
   */
  void acquire(LockType type, const char *owner);

  /**
   * Release a previously acquired lock.
   */
  void release(LockType type, const char *owner);

  /**
   * How many times is the lock currently held?
   */
  uint32_t getCount(LockType type) const;

  /**
   * Get the lock name for logging and diagnostics.
   */
  const char *getName(LockType type) const;

 private:
  Power() {};

  typedef struct {
    const esp_pm_lock_type_t type;
    const char *const name;
    esp_pm_lock_handle_t handle;
    std::atomic<uint32_t> count;
  } lock_t;

  const lock_t &getLock(LockType type) const;
  lock_t &getLock(LockType type);

  bool m_Init = false;

  std::array<lock_t, 3> m_Locks = {
      lock_t {ESP_PM_NO_LIGHT_SLEEP, "no_light_sleep", nullptr, 0},
      lock_t {ESP_PM_CPU_FREQ_MAX,   "cpu_freq_max",   nullptr, 0},
      lock_t {ESP_PM_APB_FREQ_MAX,   "apb_freq_max",   nullptr, 0},
  };
};
}  // namespace Furble

#endif
