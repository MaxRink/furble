#ifndef FURBLE_SIM_CAMERA_LIST_H
#define FURBLE_SIM_CAMERA_LIST_H

#include <cstddef>
#include <memory>
#include <vector>

#include "Camera.h"

namespace Furble {

class CameraList {
 public:
  static void save(const Camera *camera);
  static void remove(Camera *camera);
  static void load(void);
  static size_t getSaveCount(void);
  static size_t size(void);
  static void clear(void);
  // Keep the simulator surface identical to the production CameraList API so
  // transport code is compiled against the same snapshot and identity calls.
  static std::vector<std::shared_ptr<Camera>> snapshot(void);
  static std::shared_ptr<Camera> last(void);
  static std::shared_ptr<Camera> get(size_t n);
  static uint8_t getCameraId(const Camera *camera);
  static void addFauxNY(void);
};

}  // namespace Furble

#endif
