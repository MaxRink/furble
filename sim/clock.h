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
 * Report a wait on a host mutex to the simulator scheduler.
 *
 * A task that blocks on a plain host mutex leaves the scheduler still holding
 * its turn and still marked runnable, so no other task can observe the wait.
 * The turn then has to be taken away by the host-time deadlock breaker, and
 * the UI handoff burns its whole host ceiling, both of which leak host
 * scheduling into the virtual clock (issue 279). Bracketing the acquisition
 * with these makes the wait an ordinary scheduler block: the waiter stops
 * being runnable, the holder is dispatched at once, and the breaker never
 * fires on that path. This is the scheduler-visible mutex plan 158 Phase 3
 * named. Both are no-ops on a thread that is not a simulator task.
 */
void schedulerHostBlockBegin(void);
void schedulerHostBlockEnd(void);

/**
 * A mutex whose contended waits are visible to the simulator scheduler.
 *
 * An uncontended acquisition is a plain try_lock and reports nothing, so the
 * common case costs one atomic. Only a wait that would actually block reaches
 * the scheduler.
 */
class SchedulerMutex {
 public:
  void lock(void) {
    if (m_Mutex.try_lock()) {
      return;
    }
    schedulerHostBlockBegin();
    m_Mutex.lock();
    schedulerHostBlockEnd();
  }

  bool try_lock(void) { return m_Mutex.try_lock(); }

  void unlock(void) { m_Mutex.unlock(); }

 private:
  std::mutex m_Mutex;
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
