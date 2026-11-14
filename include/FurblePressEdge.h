#ifndef FURBLE_PRESS_EDGE_H
#define FURBLE_PRESS_EDGE_H

#include <cstdint>

namespace Furble {

/**
 * One press edge across every input source.
 *
 * Each source has a bit. A held source reads pressed on every sample, and one
 * tap on a touch board drives the touch source and a synthesized button
 * source together. So a press counts only when no source is held. Two
 * overlapping presses count once.
 */
struct PressEdge {
  uint8_t held = 0;

  /** Record one sample; true when it starts a press. */
  bool sample(uint8_t source, bool pressed) {
    const bool edge = pressed && (held == 0);
    held = pressed ? static_cast<uint8_t>(held | source) : static_cast<uint8_t>(held & ~source);
    return edge;
  }

  /** Mark a source released without a sample. */
  void release(uint8_t source) { held = static_cast<uint8_t>(held & ~source); }
};

}  // namespace Furble

#endif
