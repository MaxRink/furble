#pragma once

namespace m5 {
enum imu_t {
  imu_none,
  imu_unknown,
  imu_sh200q,
  imu_mpu6050,
  imu_mpu6886,
  imu_mpu9250,
  imu_bmi270,
};
}  // namespace m5

struct FurbleGestureTestImu {
  bool enabled = true;
  float x = 0.0f;
  float y = 0.0f;
  float z = 1.0f;

  m5::imu_t type = m5::imu_bmi270;

  bool isEnabled() const { return enabled; }
  m5::imu_t getType() const { return type; }
  void update() {}
  bool getAccel(float *outX, float *outY, float *outZ) const {
    if (!enabled) {
      return false;
    }
    *outX = x;
    *outY = y;
    *outZ = z;
    return true;
  }
};

struct FurbleGestureTestM5 {
  FurbleGestureTestImu Imu;
};

inline FurbleGestureTestM5 M5;
