#ifndef FURBLE_HEADLESS_CAMERA_LIST_H
#define FURBLE_HEADLESS_CAMERA_LIST_H

#include <cstdint>
#include <memory>
#include <vector>

#include "Camera.h"

namespace Furble {
class CameraList {
 public:
  static void load(void);
  static size_t getSaveCount(void);
  static size_t size(void);
  static std::shared_ptr<Camera> get(size_t index);
  static bool isSaved(const Camera *camera);
  static int32_t savedIndex(const Camera *camera);
  static std::vector<std::shared_ptr<Camera>> savedSnapshot(void);
  static bool remove(Camera *camera);
  static uint8_t getCameraId(const Camera *camera);
  static void clear(void);
  static void addFauxNY(void);
};
}  // namespace Furble

#endif
