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
 *
 * Chips that specify bus-free time between packets get it from delayUs(). The
 * default does nothing, so a host fake and a bit-banged bus need no pacing;
 * every driver that needs a wait says so next to the call, with the datasheet
 * section. A firmware adapter must override delayUs() before it drives the
 * BQ27220 gauge: the default no-op does not satisfy that part's 66 us rule,
 * and the failure is silent (the gauge answers with stale or corrupt data).
 */
class RegisterBus {
 public:
  virtual ~RegisterBus() = default;

  virtual bool read(uint8_t reg, uint8_t *data, size_t length) = 0;
  virtual bool write(uint8_t reg, const uint8_t *data, size_t length) = 0;

  /**
   * Keep the bus idle for at least `us` microseconds before the next transfer.
   *
   * This is the pacing seam, not a scheduler: the call blocks, it does not
   * yield, and it runs on the thread that owns the bus. It is separate from
   * read() and write() because the required gap belongs to the chip's command
   * grammar, not to a single transfer, and the host test can then count the
   * waits without modelling time.
   *
   * The default returns immediately and is the documented default for a bus
   * with no timing rule and for host fakes. An override must really wait at
   * least `us`: returning early leaves the next packet inside the window the
   * chip excludes.
   */
  virtual void delayUs(uint32_t us) { (void)us; }

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
