#ifndef FURBLE_SIM_CAMERA_LIST_H
#define FURBLE_SIM_CAMERA_LIST_H

#include <cstddef>
#include <memory>

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
  static std::shared_ptr<Camera> last(void);
  static std::shared_ptr<Camera> get(size_t n);
  static void addFauxNY(void);
};

}  // namespace Furble

#endif
