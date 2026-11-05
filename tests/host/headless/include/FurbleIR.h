#ifndef FURBLE_HEADLESS_IR_H
#define FURBLE_HEADLESS_IR_H

#include <cstdint>

namespace Furble {
class IR {
 public:
  enum class protocol_t : uint8_t {
    NIKON = 0,
    SONY = 1,
    CANON = 2,
    CANON_DELAYED = 3,
  };

  static IR &getInstance(void);
  static protocol_t clampProtocol(uint8_t value);
  static void init(void);
  bool isSupported(void) const;
  void fire(void);
  void fire(protocol_t protocol);
};
}  // namespace Furble

#endif
