#ifndef FURBLE_RAIL_H
#define FURBLE_RAIL_H

#include <cstdint>

namespace Furble {

/**
 * What a board's switchable 5 V rail feeds, named by circuit.
 *
 * On the M5StickS3 the rail is the M5PM1 PWR_CFG bit 3 (BOOST_EN, the EXT_5V
 * "5VINOUT" port). The M5Stack documentation has that port feeding the Grove
 * 5 V output, the HAT EXT_5V pin and the infrared emitter and receiver; see
 * docs/power/datasheet-register.md and plans/364-ext5v-rail.md. It is not a GPS
 * rail, so the holders below are the circuits that draw from it.
 */
enum class RailHolder : uint8_t {
  GPS = 0,   // the Grove Port A receiver
  IR = 1,    // the infrared emitter and receiver circuit
  GROVE = 2  // any other device powered from the Grove 5 V output
};

/** Number of distinct holders one rail can have. */
constexpr uint8_t RAIL_HOLDER_COUNT = 3;

/** One-bit accounting unit for a holder. */
constexpr uint8_t railHolderBit(RailHolder holder) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(holder));
}

/**
 * Reference count of the named holders of one switchable rail.
 *
 * One rail, one owner: every firmware client that needs the rail takes a
 * named hold through `Platform::railAcquire()`, and the rail is switched off
 * only when the last holder releases it. A cut requested while another holder
 * still needs the rail is refused, which is what stops a GPS duty-cycle
 * RAIL_OFF from cutting the infrared circuit mid-burst or browning out a Grove
 * device (plans/364).
 *
 * The count is per holder, not per call, so a repeated acquire from one task
 * cannot inflate it and a stale release cannot strand the rail on with no
 * holder left. This type is pure and lock free: the owning `Platform`
 * serializes every call and holds its lock only across the PMIC transaction,
 * never across a delay or a call into Control.
 */
class RailAccount {
 public:
  /** What a release decided, and therefore what the caller must do next. */
  struct release_t {
    // Another holder still needs the rail, so the cut is refused.
    bool held;
    // The last holder released the rail, so it must be switched off.
    bool cut;
    // This holder did not hold the rail, so nothing changed.
    bool ignored;
  };

  /**
   * Add a holder.
   *
   * @return true when this acquire must switch the rail on, that is when no
   *         holder had it before.
   */
  bool acquire(RailHolder holder) {
    const bool first = (m_Mask == 0);
    m_Mask = static_cast<uint8_t>(m_Mask | railHolderBit(holder));
    return first && holds(holder);
  }

  /**
   * Remove a holder.
   *
   * @return the decision for the caller: `cut` when this was the last holder,
   *         `held` when another holder keeps the rail up, `ignored` when the
   *         holder did not have the rail.
   */
  release_t release(RailHolder holder) {
    if (!holds(holder)) {
      return {false, false, true};
    }

    m_Mask = static_cast<uint8_t>(m_Mask & ~railHolderBit(holder));
    if (m_Mask != 0) {
      return {true, false, false};
    }
    return {false, true, false};
  }

  /** Does any holder keep the rail up? */
  bool held(void) const { return m_Mask != 0; }

  /** Is this specific holder counted? */
  bool holds(RailHolder holder) const { return (m_Mask & railHolderBit(holder)) != 0; }

  /** Holders keeping the rail up, as a bit mask. */
  uint8_t mask(void) const { return m_Mask; }

  /** Number of distinct holders. */
  uint8_t count(void) const {
    uint8_t count = 0;
    for (uint8_t bit = 0; bit < RAIL_HOLDER_COUNT; bit++) {
      if ((m_Mask & (1u << bit)) != 0) {
        count++;
      }
    }
    return count;
  }

  /** Drop every holder, for a platform reset. */
  void reset(void) { m_Mask = 0; }

 private:
  uint8_t m_Mask = 0;
};

}  // namespace Furble

#endif  // FURBLE_RAIL_H
