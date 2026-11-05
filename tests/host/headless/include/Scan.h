#ifndef FURBLE_HEADLESS_SCAN_H
#define FURBLE_HEADLESS_SCAN_H

#include <cstdint>
#include <functional>

#include "CameraList.h"

namespace Furble {
class Scan {
 public:
  enum class Mode : uint8_t {
    FULL = 0,
    BALANCED = 1,
    LOW = 2,
  };

  static Scan &getInstance(void);

  void setMode(Mode mode);
  void setTimeout(uint32_t timeout);
  bool start(std::function<void(void *)> callback,
             void *privateData,
             std::function<void(void *)> endCallback = nullptr);
  void stop(void);
  bool isActive(void) const;
  void clear(void);
  void processPendingCallbacks(void);

 private:
  Scan() = default;
  bool m_Active = false;
};
}  // namespace Furble

#endif
