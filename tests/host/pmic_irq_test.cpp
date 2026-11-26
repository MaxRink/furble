// Unit coverage for the M5PM1 IRQ status latches behind the StickS3 wake line.
// The chip user manual (V1.9) says a GPIO configured as IRQ is held low until
// the IRQ Status is cleared, and IRQ Status 2 (0x41) carries the 5VIN insert and
// remove events that nothing in furble consumes. Clearing only Status 1
// therefore left PYG1_IRQ asserted on GPIO13 after a USB insert or removal, and
// armMotionWake() then armed a level wake on a line that could never release.
//
// Scope, stated plainly: this target links include/FurblePmicIrq.h and the PMIC
// shim, nothing else. It cannot fail on the pre-fix tree, because the pre-fix
// tree has no header to test and the clear sequence it exercises is the header's
// own. The "old sequence" block below is a model baseline, not production
// coverage. The production call sites (armMotionWake, disarmMotionWake,
// motionWakeSample, clearMotionWake) are covered by the platform-wake target in
// tests/host/platform, which compiles src/FurblePlatform.cpp unchanged.
//
// The model of the chip is sim/shim/M5PM1.h, which latches both status registers
// and clears them only on a write, like the vendor driver's zero write.

#include <cstdlib>
#include <iostream>

#include "FurblePmicIrq.h"
#include "M5PM1.h"
#include "clock.h"

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

// Mirrors Platform::m5pm1Access(): the first transaction after the PMIC idle
// sleep fails and only wakes it, so every access is retried once.
template <typename Call>
bool retried(Call &&call) {
  if (call() == M5PM1_OK) {
    return true;
  }
  return call() == M5PM1_OK;
}

bool clearStatus(M5PM1 &pmic, Furble::PmicIrq::Status status) {
  return retried([&pmic, status]() {
    return (status == Furble::PmicIrq::Status::SYSTEM) ? pmic.irqClearSysAll()
                                                       : pmic.irqClearGpioAll();
  });
}

uint8_t readGpioStatus(M5PM1 &pmic, uint8_t clean) {
  uint8_t status = 0;
  check(retried([&pmic, &status, clean]() { return pmic.irqGetGpioStatus(&status, clean); }),
        "GPIO status read succeeds after the idle retry");
  return status;
}

}  // namespace

int main() {
  using Furble::PmicIrq::Status;

  M5PM1::resetPersistentStateForTest();
  Furble::Sim::setClockMillis(0);

  check(Furble::PmicIrq::SYS_5VIN_INSERT == M5PM1_IRQ_SYS_5VIN_IN,
        "the production 5VIN insert bit matches the vendor driver");
  check(Furble::PmicIrq::SYS_5VIN_REMOVE == M5PM1_IRQ_SYS_5VIN_OUT,
        "the production 5VIN remove bit matches the vendor driver");

  // Status 1 is a latch, not a level: it clears on a write, and a read without
  // a clean leaves it set.
  {
    M5PM1 pmic;
    check(pmic.begin(nullptr) == M5PM1_OK, "status 1 scenario begins");

    pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
    check(pmic.irqLineAssertedForTest(), "a latched GPIO4 edge asserts the IRQ line");

    check(readGpioStatus(pmic, M5PM1_CLEAN_NONE) == M5PM1_IRQ_GPIO4, "status 1 reports GPIO4");
    check(pmic.irqLineAssertedForTest(), "a read without a clean leaves the line asserted");

    check(readGpioStatus(pmic, M5PM1_CLEAN_ONCE) == M5PM1_IRQ_GPIO4,
          "status 1 reports GPIO4 on the clean-once read");
    check(!pmic.irqLineAssertedForTest(), "cleaning the asserted bit releases the line");

    pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
    check(retried([&pmic]() { return pmic.irqClearGpioAll(); }), "status 1 clear succeeds");
    check(pmic.gpioIrqStatusForTest() == 0, "a zero write clears every status 1 bit");
    check(!pmic.irqLineAssertedForTest(), "the status 1 clear releases the line");
  }

  // Model baseline for the defect. This is what the old code ran, replayed by
  // this test against the shim: read and clean Status 1, then clear Status 1
  // only. A 5VIN event during the session is never consumed, so the model's line
  // stays asserted with no motion pending. This proves the model can express the
  // defect, not that production regressed: the platform-wake target is the
  // production regression.
  {
    M5PM1 pmic;
    check(pmic.begin(nullptr) == M5PM1_OK, "5VIN regression scenario begins");

    pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
    pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);

    check(readGpioStatus(pmic, M5PM1_CLEAN_ONCE) == M5PM1_IRQ_GPIO4,
          "the motion poll reads and cleans GPIO4");
    check(retried([&pmic]() { return pmic.irqClearGpioAll(); }), "the old clear succeeds");
    check(pmic.gpioIrqStatusForTest() == 0, "the old clear emptied status 1");
    check(pmic.sysIrqStatusForTest() == M5PM1_IRQ_SYS_5VIN_IN,
          "no path in the old code cleared the 5VIN latch in status 2");
    check(pmic.irqLineAssertedForTest(),
          "without the fix the wake line stays asserted after a USB insert");

    // The production clear sequence releases it.
    check(
        Furble::PmicIrq::clearLatched([&pmic](Status status) { return clearStatus(pmic, status); }),
        "the production clear of both latches succeeds");
    check(pmic.gpioIrqStatusForTest() == 0, "the production clear emptied status 1");
    check(pmic.sysIrqStatusForTest() == 0, "the production clear emptied status 2");
    check(!pmic.irqLineAssertedForTest(), "clearing both latches releases the wake line");
  }

  // The arm path clears both registers on its first access after an idle PMIC,
  // so the retry inside the access wrapper is part of the contract: two
  // latched registers must not become one clear and one lost write.
  {
    M5PM1 pmic;
    check(pmic.begin(nullptr) == M5PM1_OK, "arm scenario begins");

    pmic.raiseSysIrqForTest(static_cast<uint8_t>(M5PM1_IRQ_SYS_5VIN_IN | M5PM1_IRQ_SYS_5VIN_OUT));
    check(pmic.irqLineAssertedForTest(), "the pre-arm 5VIN latch asserts the line");

    // First access after idle fails and only wakes the PMIC.
    check(
        Furble::PmicIrq::clearLatched([&pmic](Status status) { return clearStatus(pmic, status); }),
        "the production clear survives the idle-wake retry");
    check(pmic.gpioIrqStatusForTest() == 0, "status 1 is clear before the wake is armed");
    check(pmic.sysIrqStatusForTest() == 0, "status 2 is clear before the wake is armed");
    check(!pmic.irqLineAssertedForTest(), "the line is released before the level wake is armed");
  }

  // Masking is not a release in this model, which is why production clears.
  // The chip manual documents the clear as what releases the line; what a mask
  // does to the aggregated output is not documented anywhere furble relies on.
  {
    M5PM1 pmic;
    check(pmic.begin(nullptr) == M5PM1_OK, "mask scenario begins");

    pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
    pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_OUT);
    check(
        retried([&pmic]() { return pmic.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_ENABLE); }),
        "the GPIO4 mask write succeeds");
    check(retried([&pmic]() {
            return pmic.irqSetSysMask(M5PM1_IRQ_SYS_5VIN_OUT, M5PM1_IRQ_MASK_ENABLE);
          }),
          "the 5VIN mask write succeeds");
    check(pmic.gpioIrqMaskForTest() == M5PM1_IRQ_GPIO4, "the mask bit is recorded");
    check(pmic.irqLineAssertedForTest(), "a mask alone does not release a latched status");

    // The clear is the release, which is the whole reason production clears.
    check(
        Furble::PmicIrq::clearLatched([&pmic](Status status) { return clearStatus(pmic, status); }),
        "the production clear succeeds over a masked latch");
    check(!pmic.irqLineAssertedForTest(), "the clear releases what the masks left asserted");
  }

  // A PMIC that refuses the write must not report success, so a caller can log
  // it rather than assume the line released.
  {
    M5PM1 pmic;
    check(pmic.begin(nullptr) == M5PM1_OK, "failure scenario begins");
    check(retried([&pmic]() { return pmic.shutdown(); }), "the scenario shuts the PMIC down");
    check(!Furble::PmicIrq::clearLatched(
              [&pmic](Status status) { return clearStatus(pmic, status); }),
          "a refused clear reports failure");
  }

  std::cout << "pmic-irq: PASS\n";
  return 0;
}
