#ifndef FURBLE_HAL_SX1262_H
#define FURBLE_HAL_SX1262_H

#include <cstddef>
#include <cstdint>

/*
 * Semtech SX1262 LoRa radio, sleep only.
 *
 * furble has no use for the radio, but a board that carries one must hold it
 * in its lowest state or it idles in STDBY_RC at 0.6 mA (Table 3-5). Source:
 * Semtech "SX1261/2 Data Sheet", DS.SX1261-2.W.APP, Rev 2.2, December 2024.
 *
 * Sequence:
 *  1. Pulse NRESET low for 200 us. Section 8.1 needs typically 100 us. A reset
 *     reaches STDBY_RC from any state, including a sleep or RX left behind by
 *     other firmware, and SetSleep is only accepted from STDBY (13.1.1).
 *  2. Wait for BUSY low. After reset the chip calibrates and drops BUSY in
 *     STDBY_RC (sections 8.3.1 and 9.1). Cold start is 3.5 ms typical (Table
 *     8-2); the budget is 10 ms.
 *  3. SetSleep: opcode 0x84, sleepConfig 0x00 (Table 13-1, Table 13-2): cold
 *     start, RTC wake off. All blocks off, 160 nA (Table 3-5).
 *  4. Wait 500 us, the window in which the chip must not see SPI (13.1.1).
 *  5. Confirm BUSY is high. BUSY is held high for the whole sleep (9.3).
 *
 * Any falling edge on NSS wakes the chip (9.3). The board adapter must keep
 * NSS high through MCU light and deep sleep.
 */

namespace Furble {
namespace HAL {

class SX1262 {
 public:
  /** Pins and SPI the sleep sequence needs. Implemented by the board. */
  class Port {
   public:
    virtual ~Port() = default;
    /** Drive NRESET: true holds the radio in reset (pin low). */
    virtual void setReset(bool asserted) = 0;
    /** Level of the BUSY pin, true when high. */
    virtual bool busy(void) = 0;
    /** One NSS-framed SPI write. */
    virtual bool transfer(const uint8_t *tx, size_t length) = 0;
    virtual void delayUs(uint32_t us) = 0;
  };

  static constexpr uint8_t OPCODE_SET_SLEEP = 0x84;
  static constexpr uint8_t SLEEP_COLD_RTC_OFF = 0x00;
  static constexpr uint32_t RESET_PULSE_US = 200;
  static constexpr uint32_t READY_BUDGET_US = 10000;
  static constexpr uint32_t POLL_US = 100;
  static constexpr uint32_t SLEEP_SETTLE_US = 500;

  /**
   * Run the sequence above.
   *
   * @return false if BUSY never dropped (no SetSleep was sent), the SPI write
   * failed, or BUSY was not high afterwards.
   */
  static bool sleep(Port &port);
};

}  // namespace HAL
}  // namespace Furble

#endif
