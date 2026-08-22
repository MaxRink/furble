#ifndef FURBLE_SIM_SD_H
#define FURBLE_SIM_SD_H

#include <cstdint>

#include "FurbleGPX.h"
#include "FurbleSimCaps.h"

namespace Furble {
/** No-op SD service used by the host simulator. A capture run can set
 * FURBLE_SIM_SD to report a mounted card so the Storage submenu renders. */
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

  bool isSupported() const { return Furble::Sim::capEnabled("FURBLE_SIM_SD"); }
  bool request(request_t) { return isSupported(); }
  bool logPoint(const GPX::point_t &) { return false; }
  void powerOff() {}

  card_state_t cardState() const {
    return isSupported() ? card_state_t::MOUNTED : card_state_t::UNMOUNTED;
  }
  uint32_t capacityMB() const { return isSupported() ? 32768 : 0; }
  uint32_t freeMB() const { return isSupported() ? 30720 : 0; }
  uint32_t generation() const { return isSupported() ? 1 : 0; }

 private:
  SD() = default;
};
}  // namespace Furble

#endif
