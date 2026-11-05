#ifndef FURBLE_HEADLESS_CONTROL_H
#define FURBLE_HEADLESS_CONTROL_H

#include <cstddef>
#include <memory>

#include "Camera.h"

namespace Furble {
class Control {
 public:
  enum state_t {
    STATE_IDLE,
    STATE_CONNECT,
    STATE_CONNECTING,
    STATE_CONNECT_FAILED,
    STATE_ACTIVE,
    STATE_DISCONNECTING,
  };

  static Control &getInstance(void);
  void addActive(std::shared_ptr<Camera> camera);
  void connectAll(bool infiniteReconnect);
  void disconnect(void);
  state_t getState(void) const;
  size_t getTargetCount(void) const;
};
}  // namespace Furble

extern "C" void control_task(void *param);

#endif
