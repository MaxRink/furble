#include "FurbleIMU.h"

namespace Furble {
namespace IMU {

std::unique_ptr<MotionBackend> createBMI270Backend(void) {
  return nullptr;
}

std::unique_ptr<MotionBackend> createMPU6886Backend(void) {
  return nullptr;
}

}  // namespace IMU
}  // namespace Furble
