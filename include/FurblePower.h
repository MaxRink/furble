#ifndef FURBLE_POWER_H
#define FURBLE_POWER_H

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

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

  static constexpr size_t LOCK_COUNT = 3;
  static constexpr size_t OWNER_SLOTS = 8;

  typedef struct {
    const char *owner;
    uint32_t acquires;
  } owner_stats_t;

  typedef struct {
    uint32_t count;
    uint32_t totalAcquires;
    int64_t heldSinceUs;
    uint64_t totalHeldUs;
    std::array<owner_stats_t, OWNER_SLOTS> owners;
  } stats_t;

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

  /**
   * Get a consistent snapshot of one lock's counters.
   *
   * totalHeldUs includes the current hold, if the lock is held when the
   * snapshot is taken.
   */
  stats_t getStats(LockType type) const;

 private:
  Power() {};

  typedef struct {
    const char *owner = nullptr;
    uint32_t acquires = 0;
  } owner_t;

  typedef struct {
    const esp_pm_lock_type_t type;
    const char *const name;
    esp_pm_lock_handle_t handle;
    std::atomic<uint32_t> count;
    std::atomic<uint32_t> totalAcquires;
    std::atomic<int64_t> heldSinceUs;
    std::atomic<int64_t> totalHeldUs;
    std::array<owner_t, OWNER_SLOTS> owners;
  } lock_t;

  const lock_t &getLock(LockType type) const;
  lock_t &getLock(LockType type);
  static void recordOwner(lock_t &lock, const char *owner);

  bool m_Init = false;
  mutable std::mutex m_StatsMutex;

  std::array<lock_t, LOCK_COUNT> m_Locks = {
      lock_t {ESP_PM_NO_LIGHT_SLEEP, "no_light_sleep", nullptr, 0, 0, 0, 0, {}},
      lock_t {ESP_PM_CPU_FREQ_MAX,   "cpu_freq_max",   nullptr, 0, 0, 0, 0, {}},
      lock_t {ESP_PM_APB_FREQ_MAX,   "apb_freq_max",   nullptr, 0, 0, 0, 0, {}},
  };
};
}  // namespace Furble

#endif
