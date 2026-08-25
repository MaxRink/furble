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

  // Every PMIC operation has a negative path. The production wrapper must
  // retry once, and callers such as flash preparation must fail closed when
  // both attempts fail.
  M5PM1::resetPersistentStateForTest();
  setClockMillis(0);
  M5PM1 faults;
  check(faults.begin(nullptr) == M5PM1_OK, "fault-injection PMIC begins");
  M5PM1::failNextForTest(false, false, true, false);
  check(faults.setDownloadLock(false) == M5PM1_ERROR,
        "download unlock wake precedes injected failure");
  check(faults.setDownloadLock(false) == M5PM1_ERROR,
        "download unlock write reports injected failure");
  check(faults.setDownloadLock(false) == M5PM1_OK,
        "download unlock remains recoverable after injected failure");

  M5PM1::failNextForTest(false, false, false, true);
  bool faultLock = false;
  check(faults.getDownloadLock(&faultLock) == M5PM1_ERROR,
        "download lock read wake precedes injected failure");
  check(faults.getDownloadLock(&faultLock) == M5PM1_ERROR,
        "download lock read reports injected failure");
  check(faults.getDownloadLock(&faultLock) == M5PM1_OK,
        "download lock read recovers after injected failure");

  M5PM1::failNextForTest(true, false, false, false);
  check(faults.wdtSet(0) == M5PM1_ERROR,
        "watchdog disable wake precedes injected failure");
  check(faults.wdtSet(0) == M5PM1_ERROR,
        "watchdog disable reports injected write failure");
  check(faults.wdtSet(0) == M5PM1_OK,
        "watchdog disable recovers after injected write failure");

  M5PM1::failNextForTest(false, true, false, false);
  uint8_t faultCount = 0;
  check(faults.wdtGetCount(&faultCount) == M5PM1_ERROR,
        "watchdog readback wake precedes injected failure");
  check(faults.wdtGetCount(&faultCount) == M5PM1_ERROR,
        "watchdog readback reports injected failure");
  check(faults.wdtGetCount(&faultCount) == M5PM1_OK && faultCount == 0,
        "watchdog readback recovers and confirms disabled state");

  // A failed restoration must not be mistaken for an armed watchdog. The
  // retained PMIC state remains disabled until a verified arm succeeds.
  M5PM1::failNextForTest(true, false, false, false);
  check(faults.wdtSet(2) == M5PM1_ERROR,
        "watchdog restore wake precedes injected failure");
  check(faults.wdtSet(2) == M5PM1_ERROR,
        "watchdog restore reports injected failure");
  faultCount = 1;
  check(faults.wdtGetCount(&faultCount) == M5PM1_OK && faultCount == 0,
        "failed watchdog restore leaves the watchdog disabled and observable");

  std::cout << "pmic recovery persistence tests passed\n";
  return 0;
}
