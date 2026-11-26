#ifndef FURBLE_PIN_SHARE_H
#define FURBLE_PIN_SHARE_H

#include <cstdint>

namespace Furble {

/**
 * Ownership of one pin shared by the feedback LED and the IR emitter.
 *
 * On the StickC Plus2 GPIO19 is both the red LED and the IR emitter, so two
 * drivers write one pad: Feedback::setLight() drives it as a plain GPIO output
 * and IR binds it to an RMT TX channel. Without an owner a blink can land in
 * the middle of a frame, and the pad stays muxed to the RMT signal after the
 * frame, which leaves the LED at the RMT idle level and makes later GPIO
 * writes inert.
 *
 * The arbitration is one small state machine plus a hand-over: Feedback keeps
 * the pin while IR is idle, IR takes it for exactly one frame, and the LED
 * level Feedback last asked for is restored when the frame ends. Boards where
 * the LED and the emitter have different pins never enter this path, and the
 * glue is inert when the board has no LED.
 *
 * Allocation-free and non-blocking: the state is three bytes, the glue takes
 * one short mutex per call, and no lock is ever held across an RMT transfer.
 */
namespace PinShare {

/** Which driver currently owns the shared pin. */
enum class Owner : uint8_t {
  NONE = 0,  // nothing has claimed the pin yet
  LED,       // Feedback owns it
  IR,        // one IR frame owns it
};

/**
 * True when a profile gives the LED and the IR emitter the same pin.
 *
 * Only the StickC Plus2 does: the StickC and the StickC Plus put the emitter
 * on G9 and the LED on G10, and the boards without an emitter or without an
 * LED keep their pins apart by construction.
 */
constexpr bool ledSharesIr(int led_pin, int ir_pin) {
  return (led_pin >= 0) && (led_pin == ir_pin);
}

/** GPIO level for a logical LED state. The StickC family LED is active low. */
constexpr uint8_t ledLevel(bool on, bool active_low) {
  return (on == active_low) ? 0 : 1;
}

/**
 * Pure ownership state for one shared pin.
 *
 * Holds no lock and calls no hardware, so the host tests drive it directly.
 * The firmware glue in src/FurblePinShare.cpp owns the single instance and
 * serialises these transitions.
 */
class Arbiter {
 public:
  /**
   * Record the level Feedback wants.
   *
   * Returns true when the LED may drive the pin now, false while an IR frame
   * owns it. The request is remembered either way so the level can be restored
   * when the frame ends.
   */
  bool requestLed(uint8_t level) {
    m_LedLevel = level;
    m_LedKnown = true;
    return m_Owner != Owner::IR;
  }

  /** Take the pin for one frame. False when a frame already owns it. */
  bool beginIr(void) {
    if (m_Owner == Owner::IR) {
      return false;
    }
    m_Owner = Owner::IR;
    return true;
  }

  /** Hand the pin back after a frame. False when IR did not own it. */
  bool endIr(void) {
    if (m_Owner != Owner::IR) {
      return false;
    }
    m_Owner = Owner::LED;
    return true;
  }

  Owner owner(void) const { return m_Owner; }

  /** True once Feedback has asked for a level on this boot. */
  bool ledKnown(void) const { return m_LedKnown; }

  /** The level Feedback last asked for. Meaningful only when ledKnown(). */
  uint8_t ledLevel(void) const { return m_LedLevel; }

 private:
  Owner m_Owner = Owner::NONE;
  uint8_t m_LedLevel = 1;  // the active-low inactive level
  bool m_LedKnown = false;
};

/*
 * The glue is compiled only where a profile gives the LED and the emitter one
 * pin, which is the Plus2 in the m5stick-c-plus image, so the capability is set
 * on the two StickC family envs and on the boundary targets that drive the
 * production drivers. It is deliberately not keyed on FURBLE_SIM: the
 * simulator shims FurbleFeedback.h and FurbleIR.h and never compiles this
 * source, so declaring the functions there would only create a link trap.
 */
#if defined(FURBLE_PIN_SHARE)

/**
 * Record a Feedback LED level and drive it while the LED owns the pin.
 *
 * Returns false when the request could not be driven: no LED on this board, or
 * an IR frame owns the pin. The request is remembered in both cases.
 */
bool requestLed(uint8_t level);

/**
 * Take the shared pin for one IR frame.
 *
 * Returns false when this board does not put the LED on the IR pin, so the
 * caller keeps its previous behaviour and skips the hand-over entirely.
 */
bool beginIr(int ir_pin);

/**
 * Hand the pin back after a frame and restore the LED state.
 *
 * Returns false when IR did not own the pin. The pad is moved back to the
 * plain GPIO output path, because the RMT driver leaves it muxed to its
 * channel signal after rmt_disable(). When Feedback never recorded a level the
 * pad is parked at the board's inactive level, which the profile row names,
 * rather than at a level invented from the frame.
 */
bool endIr(void);

#else

/*
 * This build has no LED pin, so there is nothing to arbitrate. Only the StickC
 * family puts the feedback LED on a pin; every other profile row has ledPin -1.
 * The inert forms keep the call sites readable and cost no flash: the real glue
 * is 405 bytes of .text, and m5stack-core-debug runs within a few hundred bytes
 * of its OTA slot.
 */
inline bool requestLed(uint8_t) {
  return false;
}

inline bool beginIr(int) {
  return false;
}

inline bool endIr(void) {
  return false;
}

#endif

}  // namespace PinShare
}  // namespace Furble

#endif  // FURBLE_PIN_SHARE_H
