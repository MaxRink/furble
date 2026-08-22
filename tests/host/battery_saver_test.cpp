// Host unit test for the Battery Saver override arithmetic.
//
// Battery Saver is an opt-in power profile. When on, the effective value of a
// bundle of power settings is forced to its battery-optimal value; the stored
// individual settings are never touched, so turning the profile off restores
// the user's own choices. This suite pins that override behaviour, including
// the StickS3-only gate on sleep-while-connected.
//
// Mutation check: change any kX constant in FurbleBatterySaver.h (for example
// kInactivity from 2 to 4) and the matching bundle assertion below fails.
// Restoring the value returns to all-green. That is the test's tooth.

#include <cstdint>
#include <iostream>

#include "FurbleBatterySaver.h"

namespace BS = Furble::BatterySaver;

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// Default off (profile inactive) must reproduce the stored value for every
// setting. This is the "default OFF reproduces current settings" guarantee.
bool testInactiveIsPassthrough() {
  std::cout << "test: with the profile off every setting keeps its stored value\n";
  const int before = g_Failures;

  check(BS::sleepConn(false, false, true) == false, "sleepConn off keeps stored false");
  check(BS::sleepConn(false, true, true) == true, "sleepConn off keeps stored true");
  check(BS::connSaver(false, false) == false, "connSaver off keeps stored false");
  check(BS::connSaver(false, true) == true, "connSaver off keeps stored true");
  check(BS::reconBackoff(false, false) == false, "reconBackoff off keeps stored false");
  check(BS::scanMode(false, 0) == 0, "scanMode off keeps stored full");
  check(BS::scanMode(false, 2) == 2, "scanMode off keeps stored low");
  check(BS::inactivity(false, 0) == 0, "inactivity off keeps stored never");
  check(BS::displayOff(false, 0) == 0, "displayOff off keeps stored dim");

  return g_Failures == before;
}

// With the profile on, each setting takes its battery-optimal bundle value.
bool testActiveAppliesBundle() {
  std::cout << "test: with the profile on the bundle values are forced\n";
  const int before = g_Failures;

  // sleep-while-connected is only forced on a board that supports it.
  check(BS::sleepConn(true, false, true) == true, "sleepConn on forces true on a supported board");
  check(BS::connSaver(true, false) == true, "connSaver on forces true");
  check(BS::reconBackoff(true, false) == true, "reconBackoff on forces true");
  check(BS::scanMode(true, 0) == 1, "scanMode on forces balanced (1)");
  check(BS::inactivity(true, 0) == 2, "inactivity on forces 60 seconds (coded 2)");
  check(BS::displayOff(true, 0) == 1, "displayOff on forces the off mode (1)");

  // Pin the encodings so a stray edit to a constant is caught here.
  check(BS::kScanMode == 1, "balanced scan is index 1");
  check(BS::kInactivity == 2, "60 second inactivity is coded value 2");
  check(BS::kDisplayOff == 1, "off display mode is index 1");

  return g_Failures == before;
}

// sleep-while-connected must stay at the stored value on a board that cannot do
// it safely (no modem-sleep controller, no GPS burst lock). This is the
// StickS3-only gate.
bool testSleepConnBoardGate() {
  std::cout << "test: sleep-while-connected is only forced on a supported board\n";
  const int before = g_Failures;

  check(BS::sleepConn(true, false, false) == false,
        "an unsupported board keeps sleepConn at its stored false");
  check(BS::sleepConn(true, true, false) == true,
        "an unsupported board still passes a stored true through");

  return g_Failures == before;
}

// Turning the profile off restores the user's own values, because the override
// never wrote them. This is the "toggling OFF restores prior values" guarantee.
bool testTogglingOffRestoresStored() {
  std::cout << "test: turning the profile off restores the stored values it overrode\n";
  const int before = g_Failures;

  // A user who set connSaver on and scanMode low, then rode Battery Saver, sees
  // their own values again the instant the profile is off.
  const bool storedConnSaver = true;
  const uint8_t storedScan = 2;
  const uint8_t storedInactivity = 0;

  check(BS::connSaver(true, storedConnSaver) == true, "profile on: connSaver forced true");
  check(BS::connSaver(false, storedConnSaver) == storedConnSaver,
        "profile off: connSaver back to the user's true");
  check(BS::scanMode(false, storedScan) == storedScan,
        "profile off: scanMode back to the user's low");
  check(BS::inactivity(false, storedInactivity) == storedInactivity,
        "profile off: inactivity back to the user's never");

  return g_Failures == before;
}

}  // namespace

int main() {
  testInactiveIsPassthrough();
  testActiveAppliesBundle();
  testSleepConnBoardGate();
  testTogglingOffRestoresStored();

  if (g_Failures != 0) {
    std::cout << "battery saver harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "battery saver harness: PASS\n";
  return 0;
}
