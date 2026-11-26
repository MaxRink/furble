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
  check(first.getDownloadLock(&locked) == M5PM1_ERROR, "first transaction wakes an idle PMIC");
  check(first.getDownloadLock(&locked) == M5PM1_OK && !locked, "download recovery starts unlocked");

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
  check(watchdogAfterReset.begin(nullptr) == M5PM1_OK, "watchdog PMIC instance begins");
  setClockMillis(1999);
  check(watchdogAfterReset.watchdogPowerCycles() == 0, "retained watchdog has not fired early");
  setClockMillis(2000);
  // M5PM1.h REG_WDT_CNT 0x0A resets the system at zero, so an expiry is a power
  // cycle and not a status flag.
  watchdogAfterReset.pollDeadlines();
  check(watchdogAfterReset.watchdogPowerCycles() == 1,
        "retained watchdog power-cycles the system while the ESP32 is in download mode");
  watchdogAfterReset.pollDeadlines();
  check(watchdogAfterReset.watchdogPowerCycles() == 1,
        "one expiry produces one system power cycle, not one per poll");

  // M5PM1.h REG_IRQ_STATUS1 0x40 and 0x41 are latches cleared only by a write
  // (write 0 to clear). A read must not clear them, and a masked bit must not
  // hold the aggregated line. Every other transaction only wakes the idle chip,
  // so each call goes through the same one-retry wrapper the firmware uses.
  M5PM1 latches;
  check(latches.begin(nullptr) == M5PM1_OK, "latch PMIC instance begins");
  const auto retry = [](auto call) {
    const int first = call();
    return first == M5PM1_OK ? first : call();
  };
  const auto sysStatus = [&latches, &retry](uint8_t *value) {
    return retry([&latches, value]() { return latches.irqGetSysStatus(value); });
  };
  const auto gpioStatus = [&latches, &retry](uint8_t *value, uint8_t clean) {
    return retry([&latches, value, clean]() { return latches.irqGetGpioStatus(value, clean); });
  };
  const auto setSysMask = [&latches, &retry](uint8_t pin, uint8_t mask) {
    return retry([&latches, pin, mask]() { return latches.irqSetSysMask(pin, mask); });
  };
  const auto clearSys = [&latches, &retry]() {
    return retry([&latches]() { return latches.irqClearSysAll(); });
  };

  latches.injectSysIrq(M5PM1_IRQ_SYS_5VIN_OUT);
  uint8_t status2 = 0;
  check(latches.irqGetSysStatus(&status2) == M5PM1_ERROR, "first status read wakes an idle PMIC");
  check(sysStatus(&status2) == M5PM1_OK && status2 == M5PM1_IRQ_SYS_5VIN_OUT,
        "the 5VIN latch is readable");
  check(latches.irqLineAsserted(), "an unmasked system latch holds the aggregated line");
  check(sysStatus(&status2) == M5PM1_OK && status2 == M5PM1_IRQ_SYS_5VIN_OUT,
        "reading the latch does not clear it");
  check(setSysMask(M5PM1_IRQ_SYS_5VIN_OUT, M5PM1_IRQ_MASK_ENABLE) == M5PM1_OK,
        "the 5VIN event can be masked");
  check(!latches.irqLineAsserted(), "a masked latch releases the aggregated line");
  check(setSysMask(M5PM1_IRQ_SYS_5VIN_OUT, M5PM1_IRQ_MASK_DISABLE) == M5PM1_OK,
        "the 5VIN event can be unmasked again");
  check(latches.irqLineAsserted(), "unmasking the latch asserts the line again");
  check(clearSys() == M5PM1_OK, "Status 2 clears with a write");
  check(!latches.irqLineAsserted(), "a cleared latch releases the aggregated line");

  // Status 1 is the motion path's register and clears the same way.
  latches.injectGpioIrq(M5PM1_IRQ_GPIO4);
  check(latches.irqLineAsserted(), "a GPIO4 latch holds the aggregated line");
  uint8_t status1 = 0;
  check(gpioStatus(&status1, M5PM1_CLEAN_ONCE) == M5PM1_OK && status1 == M5PM1_IRQ_GPIO4,
        "the GPIO4 latch reads back");
  check(!latches.irqLineAsserted(), "CLEAN_ONCE clears the GPIO4 latch");

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
  check(faults.wdtSet(0) == M5PM1_ERROR, "watchdog disable wake precedes injected failure");
  check(faults.wdtSet(0) == M5PM1_ERROR, "watchdog disable reports injected write failure");
  check(faults.wdtSet(0) == M5PM1_OK, "watchdog disable recovers after injected write failure");

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
  check(faults.wdtSet(2) == M5PM1_ERROR, "watchdog restore wake precedes injected failure");
  check(faults.wdtSet(2) == M5PM1_ERROR, "watchdog restore reports injected failure");
  faultCount = 1;
  check(faults.wdtGetCount(&faultCount) == M5PM1_OK && faultCount == 0,
        "failed watchdog restore leaves the watchdog disabled and observable");

  std::cout << "pmic recovery persistence tests passed\n";
  return 0;
}
