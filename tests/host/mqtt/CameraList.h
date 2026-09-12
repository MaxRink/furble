#ifndef FURBLE_HOST_MQTT_CAMERA_LIST_H
#define FURBLE_HOST_MQTT_CAMERA_LIST_H

#include <map>
#include <memory>
#include <vector>

#include "mqtt_host_dependencies.h"
#include "protocol/CameraListProtocol.h"

namespace Furble {

class CameraList {
 public:
  static void load(void) { m_Loaded = true; }
  static size_t size(void) { return m_Cameras.size(); }
  static std::shared_ptr<Camera> get(size_t index) { return m_Cameras.at(index); }
  static std::vector<std::shared_ptr<Camera>> savedSnapshot(void) {
    m_Loaded = true;
    return m_Cameras;
  }
  static uint8_t getCameraId(const Camera *camera) {
    if (camera == nullptr) {
      return CameraListProtocol::INDEX_ID_INVALID;
    }
    const auto found = m_CameraIds.find(camera->getID());
    return found == m_CameraIds.end() ? CameraListProtocol::INDEX_ID_INVALID : found->second;
  }

  static void setCameras(const std::vector<std::shared_ptr<Camera>> &cameras) {
    m_Cameras = cameras;
    for (const auto &camera : m_Cameras) {
      if ((camera != nullptr) && (m_CameraIds.find(camera->getID()) == m_CameraIds.end())
          && (m_NextId != CameraListProtocol::INDEX_ID_ALL)) {
        m_CameraIds[camera->getID()] = m_NextId++;
      }
    }
  }
  static bool loaded(void) { return m_Loaded; }
  static void reset(void) {
    m_Cameras.clear();
    m_CameraIds.clear();
    m_NextId = 1;
    m_Loaded = false;
  }

 private:
  inline static std::vector<std::shared_ptr<Camera>> m_Cameras;
  inline static std::map<std::string, uint8_t> m_CameraIds;
  inline static uint8_t m_NextId = 1;
  inline static bool m_Loaded = false;
};

}  // namespace Furble

#endif
