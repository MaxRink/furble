#include <cstdlib>
#include <iostream>

#include "M5PM1.h"
#include "clock.h"

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using Furble::Sim::setClockMillis;

  M5PM1::resetPersistentStateForTest();
  setClockMillis(0);

  M5PM1 first;
  check(first.begin(nullptr) == M5PM1_OK, "first PMIC begin succeeds");

  bool locked = true;
  check(first.getDownloadLock(&locked) == M5PM1_ERROR,
        "first transaction wakes an idle PMIC");
  check(first.getDownloadLock(&locked) == M5PM1_OK && !locked,
        "download recovery starts unlocked");

  check(first.setDownloadLock(true) == M5PM1_ERROR,
        "lock write wakes the PMIC after an idle access");
  check(first.setDownloadLock(true) == M5PM1_OK, "lock write succeeds after retry");

  // A new ESP32 instance must observe PMIC state retained across the reset.
  M5PM1 afterReset;
  check(afterReset.begin(nullptr) == M5PM1_OK, "second PMIC begin succeeds");
  locked = false;
  check(afterReset.getDownloadLock(&locked) == M5PM1_ERROR,
        "the post-reset first transaction wakes the PMIC");
  check(afterReset.getDownloadLock(&locked) == M5PM1_OK && locked,
        "download lock persists across an ESP32 reset");

  check(afterReset.setDownloadLock(false) == M5PM1_ERROR,
        "recovery unlock wakes the PMIC after verification");
  check(afterReset.setDownloadLock(false) == M5PM1_OK,
        "recovery unlock write succeeds after retry");

  M5PM1 verified;
  check(verified.begin(nullptr) == M5PM1_OK, "third PMIC begin succeeds");
  locked = true;
  check(verified.getDownloadLock(&locked) == M5PM1_ERROR,
        "the third begin still models the PMIC idle wake");
  check(verified.getDownloadLock(&locked) == M5PM1_OK && !locked,
        "recovery unlock persists after verification");

  check(verified.wdtSet(2) == M5PM1_ERROR, "watchdog arm wakes an idle PMIC");
  check(verified.wdtSet(2) == M5PM1_OK, "watchdog arm succeeds after retry");

  M5PM1 watchdogAfterReset;
  check(watchdogAfterReset.begin(nullptr) == M5PM1_OK,
        "watchdog PMIC instance begins");
  setClockMillis(1999);
  check(!watchdogAfterReset.watchdogExpired(), "retained watchdog has not expired early");
  setClockMillis(2000);
  check(watchdogAfterReset.watchdogExpired(),
        "retained watchdog expires while the ESP32 is in download mode");

  std::cout << "pmic recovery persistence tests passed\n";
  return 0;
}
