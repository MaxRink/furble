#ifndef FURBLE_SIM_CLOCK_H
#define FURBLE_SIM_CLOCK_H

#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "FurbleTime.h"

namespace Furble::Sim {

using ClockAdvanceHook = void (*)(void);
using SchedulerCallback = void (*)(void *);

uint32_t clockMillis(void);
uint64_t clockMicros(void);
/** Set virtual time without quantizing sub-millisecond timer deadlines. */
void setClockMicros(uint64_t microseconds);
void setClockMillis(uint32_t milliseconds);
/** Advance virtual time without quantizing sub-millisecond timer deadlines. */
void advanceClockMicros(uint64_t microseconds);
void advanceClock(uint32_t milliseconds);

/** Install the scheduler wake hook used to release due virtual waiters. */
void setClockAdvanceHook(ClockAdvanceHook hook);

/** Run a timer callback through the simulator's serialized scheduler gate. */
void runSchedulerTimerCallback(SchedulerCallback callback, void *argument);

/** Update timer-service readiness while schedulerMutex is already held. */
void schedulerTimerDueChanged(bool due);

/**
 * Return the lock and wakeup primitive shared by virtual-time waiters.
 * Callers must hold schedulerMutex while inspecting their own wait state.
 */
std::mutex &schedulerMutex(void);
std::condition_variable &schedulerCondition(void);

/**
 * A mutex whose contended waits are visible to the simulator scheduler.
 *
 * Ownership and waiter selection are serialized with the scheduler. Unlock
 * reserves ownership for one waiter before publishing it as runnable, so a
 * newly arriving thread cannot barge in and the UI cannot mistake the native
 * wake gap for quiescence. Firmware uses std::mutex instead of this type.
 */
struct SchedulerMutexWaiter;

/** Cooperative unwind for a non-scheduler thread cancelled while locking. */
struct SchedulerStopped {};

class SchedulerMutex {
 public:
  SchedulerMutex() = default;
  SchedulerMutex(const SchedulerMutex &) = delete;
  SchedulerMutex(SchedulerMutex &&) = delete;
  SchedulerMutex &operator=(const SchedulerMutex &) = delete;
  SchedulerMutex &operator=(SchedulerMutex &&) = delete;

  void lock(void);
  bool try_lock(void);
  void unlock(void);

 private:
  void unlinkWaiterLocked(SchedulerMutexWaiter *waiter);
  void releaseLocked(void);

  bool m_Locked = false;
  SchedulerMutexWaiter *m_Waiters = nullptr;
};

/** Return true after simulator teardown has begun. */
bool schedulerStopping(void);

/** Request teardown and wake every virtual-time waiter. */
void schedulerStop(void);

/** Clear the teardown flag for an isolated simulator/test run. */
void schedulerReset(void);

/** Return the elapsed milliseconds across the uint32_t clock wrap. */
constexpr uint32_t clockElapsed(uint32_t now, uint32_t since) {
  return Furble::Time::elapsed(now, since);
}

/** Return true when a clock deadline has been reached, including equality. */
constexpr bool clockDeadlineReached(uint32_t now, uint32_t deadline) {
  return Furble::Time::deadlineReached(now, deadline);
}

}  // namespace Furble::Sim

#endif
