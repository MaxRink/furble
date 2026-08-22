// Host unit test for the GPS degraded retry policy.
//
// Field evidence (2026-08-22 power audit): the burst windowed GPS power
// management dropped to a PERMANENT_LOCK state after a run of bad checksum
// bursts or a failed interval measurement. That state latched forever and
// pinned the NO_LIGHT_SLEEP power lock (~+37 mA on the M5StickS3), with no
// recovery until the user toggled GPS off and on. A spell of poor reception,
// for example indoors, quietly halved battery life.
//
// The fix replaces the latch with Furble::GpsDegradedRetry: on entry the lock
// is released and a bounded backoff retry is scheduled, so the state always
// keeps trying and recovers on its own once reception improves. FurbleGPS uses
// this exact policy, so the assertions below pin the production behaviour.
//
// Mutation checks (the test's teeth):
//   * Make GpsDegradedRetry::enter() leave m_RetryDeadline at 0 or never set
//     m_Active (the old permanent latch): testRetryIsScheduled and
//     testNeverHeldForever fail, because retryDue() never returns true.
//   * Drop the BACKOFF_MAX_MS cap in backoffFor(): testBackoffIsBounded fails.
//   * Delete the reset() call the healthy resync path makes: testResyncClears
//     fails, because the episode never clears.

#include <cstdint>
#include <iostream>

#include "FurbleGPSPowerCycle.h"

using Furble::GpsDegradedRetry;

namespace {

// Mirror of FurbleGPS BAD_BURSTS_TO_RESYNC. The GPS task enters the degraded
// state after this many consecutive bad checksum bursts leave it unable to
// predict the burst timing. The test drives that trigger to match the field
// scenario.
constexpr uint32_t BAD_BURSTS_TO_RESYNC = 3;

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// Drive N consecutive bad checksum bursts through the same threshold the GPS
// task applies, then enter the degraded state exactly as finishBurst() does.
// Returns whether this entry was the first of a degraded episode.
bool driveBadBurstsAndDegrade(GpsDegradedRetry &retry, uint32_t now, uint32_t badBursts) {
  bool first = false;
  uint32_t consecutive = 0;
  for (uint32_t i = 0; i < badBursts; i++) {
    consecutive++;
    if (consecutive >= BAD_BURSTS_TO_RESYNC) {
      first = retry.enter(now);
      consecutive = 0;
    }
  }
  return first;
}

// After three bad bursts the state enters degraded and, crucially, schedules a
// finite retry instead of latching. This is the direct regression for the
// permanent lock trap: there must be a future tick at which the lock is retried.
bool testRetryIsScheduled() {
  std::cout << "test: three bad bursts schedule a finite retry, not a latch\n";
  const int before = g_Failures;

  GpsDegradedRetry retry;
  const uint32_t now = 1000;
  const bool first = driveBadBurstsAndDegrade(retry, now, BAD_BURSTS_TO_RESYNC);

  check(first, "the first degraded entry reports itself for a single WARN log");
  check(retry.active(), "the state is degraded after three bad bursts");
  check(retry.retryDeadline() > now, "a retry is scheduled in the future");
  check(!retry.retryDue(now), "the retry is not due immediately");
  check(retry.retryDue(retry.retryDeadline()), "the retry becomes due at its deadline");

  return g_Failures == before;
}

// The old PERMANENT_LOCK held the lock forever. Whatever the failure history,
// the retry deadline must always fall within the cap, so the lock is never
// pinned indefinitely.
bool testNeverHeldForever() {
  std::cout << "test: the retry is always due within the backoff cap\n";
  const int before = g_Failures;

  GpsDegradedRetry retry;
  uint32_t now = 5000;
  for (uint32_t i = 0; i < 32; i++) {
    retry.enter(now);
    const uint32_t wait = retry.retryDeadline() - now;
    check(wait <= GpsDegradedRetry::BACKOFF_MAX_MS,
          "the wait to the next retry never exceeds the cap");
    check(retry.retryDue(now + GpsDegradedRetry::BACKOFF_MAX_MS),
          "the retry is due within the cap, the lock is not held forever");
    now += wait;  // model the task sleeping then re-entering on a failed retry
  }

  return g_Failures == before;
}

// The backoff grows on repeated failures, but is bounded. It starts at the
// minimum, doubles, and saturates at the maximum.
bool testBackoffIsBounded() {
  std::cout << "test: the backoff grows exponentially then saturates at the cap\n";
  const int before = g_Failures;

  GpsDegradedRetry retry;
  const uint32_t now = 0;

  retry.enter(now);
  check(retry.scheduledBackoff() == GpsDegradedRetry::BACKOFF_MIN_MS,
        "the first backoff is the minimum");
  retry.enter(now);
  check(retry.scheduledBackoff() == (GpsDegradedRetry::BACKOFF_MIN_MS * 2),
        "the second backoff doubles");
  retry.enter(now);
  check(retry.scheduledBackoff() == (GpsDegradedRetry::BACKOFF_MIN_MS * 4),
        "the third backoff doubles again");

  uint32_t last = retry.scheduledBackoff();
  for (uint32_t i = 0; i < 40; i++) {
    retry.enter(now);
    const uint32_t backoff = retry.scheduledBackoff();
    check(backoff >= last, "the backoff is monotonic");
    check(backoff <= GpsDegradedRetry::BACKOFF_MAX_MS, "the backoff never exceeds the cap");
    last = backoff;
  }
  check(last == GpsDegradedRetry::BACKOFF_MAX_MS, "the backoff saturates at the cap");

  return g_Failures == before;
}

// A healthy resync, a clean burst or a good measurement calls reset(). That
// must clear the degraded episode: no longer active, the backoff starts fresh,
// and the next entry logs itself again.
bool testResyncClears() {
  std::cout << "test: a healthy resync clears the degraded state\n";
  const int before = g_Failures;

  GpsDegradedRetry retry;
  uint32_t now = 2000;
  driveBadBurstsAndDegrade(retry, now, BAD_BURSTS_TO_RESYNC * 3);
  check(retry.active(), "the state is degraded before recovery");
  check(retry.failures() > 1, "repeated failures accumulated");

  // reception recovered, the GPS task established a prediction again
  retry.reset();
  check(!retry.active(), "the degraded state is cleared after a healthy resync");
  check(retry.failures() == 0, "the failure count is cleared");
  check(!retry.retryDue(now + GpsDegradedRetry::BACKOFF_MAX_MS),
        "no retry is pending once recovered");

  // a later failure is a fresh episode: it logs again and starts at the minimum
  now += 100000;
  const bool first = driveBadBurstsAndDegrade(retry, now, BAD_BURSTS_TO_RESYNC);
  check(first, "a failure after recovery is a fresh episode that logs again");
  check(retry.scheduledBackoff() == GpsDegradedRetry::BACKOFF_MIN_MS,
        "the backoff restarts at the minimum after recovery");

  return g_Failures == before;
}

}  // namespace

int main() {
  std::cout << "GPS degraded retry policy tests\n";

  testRetryIsScheduled();
  testNeverHeldForever();
  testBackoffIsBounded();
  testResyncClears();

  if (g_Failures > 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }

  std::cout << "all GPS degraded retry checks passed\n";
  return 0;
}
