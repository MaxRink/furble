#ifndef FURBLE_HAL_BUS_H
#define FURBLE_HAL_BUS_H

#include <cstddef>
#include <cstdint>

/*
 * Transport seams for the pure chip drivers in lib/hal.
 *
 * The drivers never include a vendor SDK or board support header. Firmware
 * wraps its I2C or SPI bus in one of these classes; host tests use a fake that
 * records every transaction. See lib/hal/CLAUDE.md.
 */

namespace Furble {
namespace HAL {

/**
 * Register access to one I2C device at a fixed address.
 *
 * read() and write() return false on any bus error. A false return leaves the
 * output buffer unspecified.
 */
class RegisterBus {
 public:
  virtual ~RegisterBus() = default;

  virtual bool read(uint8_t reg, uint8_t *data, size_t length) = 0;
  virtual bool write(uint8_t reg, const uint8_t *data, size_t length) = 0;

  bool read8(uint8_t reg, uint8_t &value) { return read(reg, &value, 1); }

  bool write8(uint8_t reg, uint8_t value) { return write(reg, &value, 1); }

  /** Read, clear mask, set bits & mask, and write back. */
  bool update8(uint8_t reg, uint8_t mask, uint8_t bits) {
    uint8_t value = 0;
    if (!read8(reg, value)) {
      return false;
    }
    return write8(reg, static_cast<uint8_t>((value & ~mask) | (bits & mask)));
  }
};

}  // namespace HAL
}  // namespace Furble

#endif
