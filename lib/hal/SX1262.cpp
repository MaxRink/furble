#include "SX1262.h"

namespace Furble {
namespace HAL {

namespace {
/**
 * Poll the BUSY pin until it reads `level`, spending at most `budgetUs` in
 * delays. Table 8-2 gives no standby-to-sleep time, so every level change the
 * sequence depends on is waited for, never sampled once.
 */
bool waitBusy(SX1262::Port &port, bool level, uint32_t budgetUs) {
  uint32_t waited = 0;
  while (port.busy() != level) {
    if (waited >= budgetUs) {
      return false;
    }
    port.delayUs(SX1262::POLL_US);
    waited += SX1262::POLL_US;
  }
  return true;
}
}  // namespace

bool SX1262::sleep(Port &port) {
  port.setReset(true);
  port.delayUs(RESET_PULSE_US);
  port.setReset(false);

  // Step 2: BUSY drops once the post-reset calibration has run (8.3.1, 9.1).
  if (!waitBusy(port, false, READY_BUDGET_US)) {
    return false;
  }

  const uint8_t command[2] = {OPCODE_SET_SLEEP, SLEEP_COLD_RTC_OFF};
  if (!port.transfer(command, sizeof(command))) {
    return false;
  }

  // Step 4: the window in which the chip must not see SPI (13.1.1).
  port.delayUs(SLEEP_SETTLE_US);

  // Step 5: BUSY is held high for the whole sleep (9.3), but no document
  // bounds how soon the chip asserts it, so wait for the level inside a
  // budget instead of sampling once at the end of the settle window.
  return waitBusy(port, true, SLEEP_CONFIRM_BUDGET_US);
}

}  // namespace HAL
}  // namespace Furble
