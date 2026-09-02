// Host infrared shim for the console command suite.
//
// The real emitter drives the RMT peripheral. The double records each fire and
// lets a test set whether the board reports an emitter, which is the branch
// 'ir fire' checks before it does anything else.
#ifndef FURBLE_IR_H
#define FURBLE_IR_H

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

  bool isSupported(void) const;
  void fire(void);
  void fire(protocol_t protocol);

 private:
  IR() = default;
};

}  // namespace Furble

#endif
