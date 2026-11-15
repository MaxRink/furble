#include "SX1262.h"

namespace Furble {
namespace HAL {

bool SX1262::sleep(Port &port) {
  port.setReset(true);
  port.delayUs(RESET_PULSE_US);
  port.setReset(false);

  uint32_t waited = 0;
  while (port.busy()) {
    if (waited >= READY_BUDGET_US) {
      return false;
    }
    port.delayUs(POLL_US);
    waited += POLL_US;
  }

  const uint8_t command[2] = {OPCODE_SET_SLEEP, SLEEP_COLD_RTC_OFF};
  if (!port.transfer(command, sizeof(command))) {
    return false;
  }
  port.delayUs(SLEEP_SETTLE_US);
  return port.busy();
}

}  // namespace HAL
}  // namespace Furble
