// Host unit test for the reconnect backoff curve.
//
// Hardware evidence (2026-08-21): after #121 fixed the reconnect hang, a
// stale-session reconnect still felt slow (~45 s). The first connect attempt
// fast-failed, then furble waited a full 17 s before the first retry, even
// though the camera usually releases its previous BLE session within a couple
// of seconds. This suite pins the corrected curve: the first retry must fire
// quickly, then persistent failures back off exponentially up to the cap.
//
// Mutation check: set ReconnectBackoff::FIRST_RETRY_MS back to 17000 (the old
// stale-session wait) and testFirstRetryIsQuick() fails its bound. Restoring
// the short value returns to all-green. That is the test's tooth.
//
// The furble-initiated first retry (task #54) is the second tooth: when furble
// itself caused the prior disconnect there is no stale peer session to wait out,
// so the first retry is immediate. Reverting delayMs() to ignore the
// furbleInitiated flag (always FIRST_RETRY_MS at attempt 0) fails
// testFurbleInitiatedFirstRetryIsImmediate().

#include <cstdint>
#include <iostream>

#include "FurbleReconnectBackoff.h"

using Furble::ReconnectBackoff::BASE_MS;
using Furble::ReconnectBackoff::delayMs;
using Furble::ReconnectBackoff::FIRST_RETRY_MS;
using Furble::ReconnectBackoff::MAX_MS;

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// The first retry after a failed reconnect must be quick. The old code stalled
// for 17 s here; a session the camera clears in a few seconds should reconnect
// in a few seconds. The bound (<= 3 s) is tight enough that the old 17 s value
// fails it, and the gap is still non-zero so furble does not spin.
bool testFirstRetryIsQuick() {
  std::cout << "test: the first reconnect retry fires quickly\n";
  const int before = g_Failures;

  const uint32_t backoffOn = delayMs(0, true);
  const uint32_t backoffOff = delayMs(0, false);

  check(backoffOn <= 3000, "the first retry waits at most 3 s with backoff enabled");
  check(backoffOff <= 3000, "the first retry waits at most 3 s with backoff disabled");
  check(backoffOn >= 1000, "the first retry keeps a minimum gap so it does not spin");
  check(backoffOn == backoffOff, "the first retry is the same short wait regardless of backoff");

  return g_Failures == before;
}

// When furble itself initiated the prior disconnect (task #54) the camera holds
// no stale session, so the first retry is immediate rather than the
// FIRST_RETRY_MS stale-session wait. A peer-initiated drop keeps FIRST_RETRY_MS.
// Only the first retry (attempt 0) is affected; the later curve is unchanged.
bool testFurbleInitiatedFirstRetryIsImmediate() {
  std::cout << "test: a furble-initiated first retry skips the stale-session wait\n";
  const int before = g_Failures;

  // Furble-initiated fast path: immediate, and strictly faster than the
  // peer-initiated stale-session wait.
  check(delayMs(0, false, /*furbleInitiated=*/true) == 0,
        "a furble-initiated first retry is immediate with backoff disabled");
  check(delayMs(0, true, /*furbleInitiated=*/true) == 0,
        "a furble-initiated first retry is immediate with backoff enabled");
  check(delayMs(0, true, /*furbleInitiated=*/true) < FIRST_RETRY_MS,
        "a furble-initiated first retry is faster than the peer-initiated wait");

  // Peer-initiated path is unchanged: it still waits FIRST_RETRY_MS.
  check(delayMs(0, false, /*furbleInitiated=*/false) == FIRST_RETRY_MS,
        "a peer-initiated first retry keeps the stale-session wait");
  check(delayMs(0, true, /*furbleInitiated=*/false) == FIRST_RETRY_MS,
        "a peer-initiated first retry keeps the stale-session wait with backoff on");

  // The flag only touches the first retry: later attempts are identical either
  // way, so the fix cannot regress the persistent-failure curve.
  for (uint32_t attempt = 1; attempt <= 8; attempt++) {
    check(delayMs(attempt, true, true) == delayMs(attempt, true, false),
          "furble-initiated does not change the backoff curve after the first retry");
    check(delayMs(attempt, false, true) == delayMs(attempt, false, false),
          "furble-initiated does not change the flat curve after the first retry");
  }

  return g_Failures == before;
}

// With backoff disabled, retries after the first hold a steady base wait.
bool testBackoffDisabledIsFlat() {
  std::cout << "test: backoff disabled holds a flat base wait after the first retry\n";
  const int before = g_Failures;

  for (uint32_t attempt = 1; attempt <= 8; attempt++) {
    check(delayMs(attempt, false) == BASE_MS, "each later retry waits the base interval");
  }

  return g_Failures == before;
}

// With backoff enabled, persistent failures grow exponentially from the base
// and saturate at the cap. This is the unchanged pre-existing curve for
// attempt >= 1, so the fix does not regress the infinite-reconnect-on-drop
// behaviour.
bool testBackoffEnabledGrowsToCap() {
  std::cout << "test: backoff enabled grows exponentially and saturates at the cap\n";
  const int before = g_Failures;

  check(delayMs(1, true) == BASE_MS * 2, "retry 1 doubles the base");
  check(delayMs(2, true) == BASE_MS * 4, "retry 2 quadruples the base");
  check(delayMs(3, true) == BASE_MS * 8, "retry 3 is eight times the base");

  // The curve is monotonic and never exceeds the cap.
  uint32_t previous = delayMs(1, true);
  for (uint32_t attempt = 2; attempt <= 12; attempt++) {
    const uint32_t delay = delayMs(attempt, true);
    check(delay >= previous, "the backoff never shrinks as failures persist");
    check(delay <= MAX_MS, "the backoff never exceeds the cap");
    previous = delay;
  }
  check(delayMs(12, true) == MAX_MS, "a long run of failures saturates at the cap");

  return g_Failures == before;
}

}  // namespace

int main() {
  testFirstRetryIsQuick();
  testFurbleInitiatedFirstRetryIsImmediate();
  testBackoffDisabledIsFlat();
  testBackoffEnabledGrowsToCap();

  if (g_Failures != 0) {
    std::cout << "reconnect backoff harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "reconnect backoff harness: PASS\n";
  return 0;
}
