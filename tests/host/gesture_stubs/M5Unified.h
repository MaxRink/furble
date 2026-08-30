#pragma once

struct FurbleGestureTestImu {
  bool enabled = true;
  float x = 0.0f;
  float y = 0.0f;
  float z = 1.0f;

  bool isEnabled() const { return enabled; }
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
