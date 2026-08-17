#ifndef FURBLE_SIM_CAMERA_LIST_H
#define FURBLE_SIM_CAMERA_LIST_H

#include <cstddef>

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
  static Camera *last(void);
  static Camera *get(size_t n);
  static void addFauxNY(void);
};

}  // namespace Furble

#endif
