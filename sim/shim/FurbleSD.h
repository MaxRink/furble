#ifndef FURBLE_SIM_SD_H
#define FURBLE_SIM_SD_H

#include <cstdint>

#include "FurbleGPX.h"

namespace Furble {
/** No-op SD service used by the host simulator. */
class SD {
 public:
  enum class request_t : uint8_t {
    POINT,
    CLOSE,
    MOUNT,
    PAGE_LEAVE,
    RELOAD,
    EXPORT,
    IMPORT,
    POWER_OFF,
  };

  enum class card_state_t : uint8_t {
    UNMOUNTED,
    MOUNTED,
    FAILED,
  };

  static SD &getInstance() {
    static SD instance;
    return instance;
  }

  static void init() {}

  bool isSupported() const { return false; }
  bool request(request_t) { return false; }
  bool logPoint(const GPX::point_t &) { return false; }
  void powerOff() {}

  card_state_t cardState() const { return card_state_t::UNMOUNTED; }
  uint32_t capacityMB() const { return 0; }
  uint32_t freeMB() const { return 0; }
  uint32_t generation() const { return 0; }

 private:
  SD() = default;
};
}  // namespace Furble

#endif
