#ifndef FURBLE_HEADLESS_CAMERA_H
#define FURBLE_HEADLESS_CAMERA_H

#include <cstdint>
#include <string>

namespace Furble {
class Camera {
 public:
  std::string getName() const { return "mock"; }
  uint32_t getType() const { return 0; }
  bool isActive() const { return m_Active; }
  void setActive(bool active) { m_Active = active; }

 private:
  bool m_Active = false;
};
}  // namespace Furble

#endif
