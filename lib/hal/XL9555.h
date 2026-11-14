#ifndef FURBLE_HAL_XL9555_H
#define FURBLE_HAL_XL9555_H

#include <cstdint>

#include "HalBus.h"

/*
 * XINLUDA XL9555 16-bit I2C IO expander.
 *
 * Source: XINLUDA "XL9535, XL9555 Product datasheet", Rev 2.3. Address byte
 * 0100 A2 A1 A0 (section 9.1, Table 3), so 7-bit addresses 0x20 to 0x27.
 * Command bytes 0x00/0x01 input, 0x02/0x03 output, 0x04/0x05 polarity,
 * 0x06/0x07 configuration (section 9.2.1, Table 6). A configuration bit of 1
 * is an input with the XL9555's weak pull-up, 0 is an output (section 9.2.5).
 * Pins 0-7 are port 0, pins 8-15 are port 1.
 *
 * Every call is a read-modify-write of the live register, so the driver holds
 * no cache that an expander reset could invalidate.
 */

namespace Furble {
namespace HAL {

class XL9555 {
 public:
  static constexpr uint8_t ADDRESS_BASE = 0x20;
  static constexpr uint8_t PIN_COUNT = 16;

  explicit XL9555(RegisterBus &bus) : m_Bus(bus) {}

  /** Configure pin as an input (with the internal weak pull-up). */
  bool setInput(uint8_t pin);

  /**
   * Configure pin as an output driving level.
   *
   * The output register is written before the direction, so the pin never
   * drives the stale power-on level (1, Table 6) for a moment. That matters
   * for an active-high enable such as a GNSS rail.
   */
  bool setOutput(uint8_t pin, bool level);

  /** Change the level of a pin already configured as an output. */
  bool write(uint8_t pin, bool level);

  /** Read the pin level from the input port register. */
  bool read(uint8_t pin, bool &level);

 private:
  RegisterBus &m_Bus;
};

}  // namespace HAL
}  // namespace Furble

#endif
