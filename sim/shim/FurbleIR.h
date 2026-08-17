#ifndef FURBLE_SIM_FURBLE_IR_H
#define FURBLE_SIM_FURBLE_IR_H

#include <cstdint>

namespace Furble {

// The simulator has no RMT peripheral. The fake reports unsupported so the
// IR trigger and its settings menu stay hidden, which keeps the scripted
// menu routes at their existing positions.
class IR {
 public:
  enum class protocol_t : uint8_t {
    NIKON = 0,
    SONY = 1,
    CANON = 2,
    CANON_DELAYED = 3,
  };

  static IR &getInstance(void) {
    static IR instance;
    return instance;
  }

  static protocol_t clampProtocol(uint8_t value) {
    if (value > static_cast<uint8_t>(protocol_t::CANON_DELAYED)) {
      return protocol_t::NIKON;
    }
    return static_cast<protocol_t>(value);
  }

  static void init(void) {}

  bool isSupported(void) const { return false; }

  void fire(void) {}

  void fire(protocol_t protocol) { (void)protocol; }

 private:
  IR() = default;
};

}  // namespace Furble

#endif
