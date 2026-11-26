#ifndef FURBLE_PMIC_IRQ_H
#define FURBLE_PMIC_IRQ_H

#include <cstdint>

namespace Furble {
namespace PmicIrq {

/**
 * The M5PM1 latch registers that can hold its aggregated IRQ line asserted.
 *
 * M5PM1 Chip User Manual V1.9 register map:
 *   0x40 IRQ Status 1: GPIO0 to GPIO4 edge latches.
 *   0x41 IRQ Status 2: 5VIN insert (bit 0) and remove (bit 1), 5VINOUT insert
 *        (bit 2) and remove (bit 3), battery insert (bit 4) and remove (bit 5).
 * Masks 0x43 (GPIO) and 0x44 (system) power up unmasked.
 *
 * Two separate sources, kept apart on purpose: the manual's status note states
 * the hold rule, "a GPIO configured as IRQ is held low until the IRQ Status is
 * cleared", and the vendor driver supplies the clear mechanism, since
 * irqClearGpioAll() and irqClearSysAll() both write 0x00 to the register. The
 * manual is not the source for the zero write.
 */
enum class Status : uint8_t {
  GPIO,    // 0x40, consumed for the GPIO4 motion event
  SYSTEM,  // 0x41, nothing in furble consumes a bit
};

/** IRQ Status 2 bits, named for readers and tests. */
constexpr uint8_t SYS_5VIN_INSERT = 1 << 0;
constexpr uint8_t SYS_5VIN_REMOVE = 1 << 1;

/**
 * Clear both status latches through the caller's access wrapper.
 *
 * Both registers are cleared rather than the status 2 bits being masked.
 * Clearing is the release mechanism the manual documents, while what a mask
 * does to the aggregated line is not documented at all, and a masked latch
 * would still be set for whatever reads status 2 next. Nothing writes the mask
 * registers today either, so a boot-time mask would be the only thing keeping
 * a stale 5VIN event off the line, and a later unmask would let it through.
 *
 * `access(Status)` performs one clear under the caller's retry policy and
 * returns true when the write was accepted. The retry matters: the M5PM1
 * needs it on the first transaction after its idle sleep. Both writes are
 * idempotent, so a retried clear is safe.
 *
 * Status 1 is written first, and the order is deliberate. Status 1 is the
 * register the motion poll reads and clears, so a GPIO4 edge that lands between
 * the two writes is latched after this sequence has passed that register and
 * survives for the next poll. Clearing Status 2 first would clear Status 1
 * second and swallow exactly that edge for no gain: Status 2 carries 5VIN
 * insert and remove, which nothing consumes, so a late 5VIN bit is harmless
 * and is cleared by the next sample or clear.
 *
 * The result is true only when both writes were accepted, so a caller that
 * arms a level wake on the line (Platform::armMotionWake()) fails rather than
 * arming on an unproven release.
 */
template <typename Access>
bool clearLatched(Access &&access) {
  const bool gpio = access(Status::GPIO);
  const bool system = access(Status::SYSTEM);
  return gpio && system;
}

}  // namespace PmicIrq
}  // namespace Furble

#endif  // FURBLE_PMIC_IRQ_H
