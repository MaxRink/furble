#ifndef FURBLE_HOST_MQTT_CAMERA_LIST_H
#define FURBLE_HOST_MQTT_CAMERA_LIST_H

#include <memory>
#include <vector>

#include "mqtt_host_dependencies.h"

namespace Furble {

class CameraList {
 public:
  static void load(void) { m_Loaded = true; }
  static size_t size(void) { return m_Cameras.size(); }
  static std::shared_ptr<Camera> get(size_t index) { return m_Cameras.at(index); }

  static void setCameras(const std::vector<std::shared_ptr<Camera>> &cameras) {
    m_Cameras = cameras;
  }
  static bool loaded(void) { return m_Loaded; }
  static void reset(void) {
    m_Cameras.clear();
    m_Loaded = false;
  }

 private:
  inline static std::vector<std::shared_ptr<Camera>> m_Cameras;
  inline static bool m_Loaded = false;
};

}  // namespace Furble

#endif
