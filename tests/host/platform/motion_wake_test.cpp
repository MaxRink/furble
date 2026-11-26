// Regression guard for the four production M5PM1 IRQ call sites behind the
// StickS3 wake line. Unlike tests/host/pmic_irq_test.cpp, which drives the clear
// sequence on its own, this target compiles the real src/FurblePlatform.cpp and
// calls armMotionWake(), motionWakeSample(), clearMotionWake() and
// disarmMotionWake() against the retained PMIC model in sim/shim/M5PM1.h.
//
// The defect: the M5PM1 holds its aggregated IRQ line (PYG1_IRQ) asserted until
// the matching IRQ Status register is cleared. Status 2 (0x41) latches 5VIN
// insert and remove, nothing in furble consumes those bits, and the old code
// cleared Status 1 only. A USB insert or removal then held the line for the rest
// of the session, so armMotionWake() armed a level-triggered light-sleep source
// on a line that could never release, and motionWakeSample() kept reporting an
// idle IMU.
//
// The shim keeps its retained state in a static, so the test's own M5PM1 handle
// observes and drives exactly what the production instance did.

#include <cstdlib>
#include <iostream>

#include "FurblePlatform.h"
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
  M5PM1::resetPersistentStateForTest();
  Furble::Sim::setClockMillis(0);

  // The test's handle into the retained model. It is never begun, so it never
  // touches the idle-wake bookkeeping the production instance owns.
  M5PM1 pmic;

  // The real boot path: M5.begin() no-ops, the PMIC is begun, download recovery
  // is unlocked through the retried access wrapper.
  auto &platform = Furble::Platform::getInstance();

  // A 5VIN event before the wake is armed leaves Status 2 latched.
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);
  check(pmic.irqLineAssertedForTest(), "the pre-arm 5VIN latch asserts the wake line");

  // 1. armMotionWake() must release that latch before arming a level wake.
  check(platform.armMotionWake(), "armMotionWake failed on the StickS3 PMIC path");
  check(pmic.gpioIrqStatusForTest() == 0, "armMotionWake left IRQ Status 1 latched");
  check(pmic.sysIrqStatusForTest() == 0, "armMotionWake left IRQ Status 2 latched");
  check(!pmic.irqLineAssertedForTest(), "armMotionWake armed on an asserted wake line");
  check(pmic.gpioFuncForTest(M5PM1_GPIO_NUM_4) == M5PM1_GPIO_FUNC_IRQ,
        "the arm did not configure GPIO4 as an IRQ source");
  check(pmic.gpioWakeEnabledForTest(M5PM1_GPIO_NUM_4), "the arm did not enable the GPIO4 wake");
  check(pmic.gpioIrqMaskForTest() == 0, "the arm did not unmask GPIO4");
  std::cout << "platform-wake arm=released\n";

  // 2. motionWakeSample() sees the GPIO4 edge and must also clear the 5VIN
  // latch, or the line stays asserted while the poll reports an idle IMU.
  pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);
  check(pmic.irqLineAssertedForTest(), "the latched events assert the wake line");
  check(platform.motionWakeSample(), "motionWakeSample missed the latched GPIO4 edge");
  check(pmic.gpioIrqStatusForTest() == 0, "motionWakeSample left IRQ Status 1 latched");
  check(pmic.sysIrqStatusForTest() == 0, "motionWakeSample left IRQ Status 2 latched");
  check(!pmic.irqLineAssertedForTest(), "motionWakeSample left the wake line asserted");
  std::cout << "platform-wake sample=released\n";

  // 3. A 5VIN event with no motion must still release the line. This is the
  // case the old code could not see: the poll returns false and used to leave
  // the 5VIN bit holding PYG1_IRQ low forever.
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_OUT);
  check(pmic.irqLineAssertedForTest(), "the 5VIN remove latch asserts the wake line");
  check(!platform.motionWakeSample(), "a 5VIN event alone reported motion");
  check(pmic.sysIrqStatusForTest() == 0, "a 5VIN-only sample left IRQ Status 2 latched");
  check(!pmic.irqLineAssertedForTest(), "a 5VIN-only sample left the wake line asserted");
  std::cout << "platform-wake sample_5vin_only=released\n";

  // 4. clearMotionWake() runs on the wake edge, so it must clear both latches.
  pmic.raiseGpioIrqForTest(M5PM1_IRQ_GPIO4);
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);
  platform.clearMotionWake();
  check(pmic.gpioIrqStatusForTest() == 0, "clearMotionWake left IRQ Status 1 latched");
  check(pmic.sysIrqStatusForTest() == 0, "clearMotionWake left IRQ Status 2 latched");
  check(!pmic.irqLineAssertedForTest(), "clearMotionWake left the wake line asserted");
  std::cout << "platform-wake clear=released\n";

  // 5. disarmMotionWake() turns the source off and clears both latches, so a
  // 5VIN event that arrived while armed does not outlive the wake source.
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);
  platform.disarmMotionWake();
  check(!pmic.gpioWakeEnabledForTest(M5PM1_GPIO_NUM_4), "disarm left the GPIO4 wake enabled");
  check(pmic.gpioIrqMaskForTest() == M5PM1_IRQ_GPIO4, "disarm did not mask GPIO4");
  check(pmic.sysIrqStatusForTest() == 0, "disarm left IRQ Status 2 latched");
  check(!pmic.irqLineAssertedForTest(), "disarm left the wake line asserted");
  check(!platform.motionWakeSample(), "a disarmed wake still reported a sample");
  std::cout << "platform-wake disarm=released\n";

  // 6. Arming again after a disarm has to work, including the idle-wake retry
  // the access wrapper hides.
  pmic.raiseSysIrqForTest(M5PM1_IRQ_SYS_5VIN_IN);
  check(platform.armMotionWake(), "the second armMotionWake failed");
  check(pmic.gpioWakeEnabledForTest(M5PM1_GPIO_NUM_4), "the second arm did not enable the wake");
  check(!pmic.irqLineAssertedForTest(), "the second arm left the wake line asserted");
  std::cout << "platform-wake rearm=released\n";

  std::cout << "platform-wake: PASS\n";
  return 0;
}
