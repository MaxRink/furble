// Injected IMU state for the host build.
//
// The firmware reads the accelerometer and gyroscope through M5.Imu. The
// simulator has no sensor, so this module holds a settable IMU state that the
// firmware reads through the same enabled, update, getAccel and getGyro surface
// under FURBLE_SIM. A scenario drives the device flat, tilted or on its side and
// the real level, gesture and motion code paths respond exactly as they do to
// the hardware IMU. The state is general on purpose, so the later motion
// features (gestures, wake on motion, gps motion) can drive it too.

#include <atomic>
#include <cmath>

#include "driver.h"

namespace Furble::Sim {

namespace {
// The simulator runs the UI, the scenario driver and the level timer on the
// single LVGL task, so no locking is needed for correctness. std::atomic keeps
// the reads and writes well defined and matches the rig state convention.
std::atomic<bool> g_enabled {true};
std::atomic<float> g_accel[3] = {{0.0f}, {0.0f}, {1.0f}};
std::atomic<float> g_gyro[3] = {{0.0f}, {0.0f}, {0.0f}};
std::atomic<bool> g_accelAvailable {true};
std::atomic<bool> g_gyroAvailable {true};
std::atomic<imu_chip_t> g_chip {imu_chip_t::NONE};
}  // namespace

void imuSetEnabled(bool enabled) {
  g_enabled.store(enabled);
}

bool imuEnabled(void) {
  return g_enabled.load();
}

void imuUpdate(void) {
  // The hardware IMU latches a fresh sample here. The injected state is already
  // current, so there is nothing to do.
}

void imuSetAccel(float x, float y, float z) {
  g_accel[0].store(x);
  g_accel[1].store(y);
  g_accel[2].store(z);
}

bool imuGetAccel(float *x, float *y, float *z) {
  if (!g_enabled.load() || !g_accelAvailable.load()) {
    return false;
  }
  if (x != nullptr) {
    *x = g_accel[0].load();
  }
  if (y != nullptr) {
    *y = g_accel[1].load();
  }
  if (z != nullptr) {
    *z = g_accel[2].load();
  }
  return true;
}

void imuSetGyro(float x, float y, float z) {
  g_gyro[0].store(x);
  g_gyro[1].store(y);
  g_gyro[2].store(z);
}

bool imuGetGyro(float *x, float *y, float *z) {
  if (!g_enabled.load() || !g_gyroAvailable.load()) {
    return false;
  }
  if (x != nullptr) {
    *x = g_gyro[0].load();
  }
  if (y != nullptr) {
    *y = g_gyro[1].load();
  }
  if (z != nullptr) {
    *z = g_gyro[2].load();
  }
  return true;
}

void imuSetAccelAvailable(bool available) {
  g_accelAvailable.store(available);
}

void imuSetGyroAvailable(bool available) {
  g_gyroAvailable.store(available);
}

void imuSetChip(imu_chip_t chip) {
  g_chip.store(chip);
}

imu_chip_t imuChip(void) {
  return g_chip.load();
}

void imuSetOrientation(float rollDeg, float pitchDeg) {
  constexpr float degreesToRadians = 0.0174532925f;
  const float roll = rollDeg * degreesToRadians;
  const float pitch = pitchDeg * degreesToRadians;
  // Unit gravity vector that reproduces the requested roll and pitch through the
  // spirit level maths: roll = atan2(ay, az), pitch = atan2(-ax, hypot(ay, az)).
  const float ax = -std::sin(pitch);
  const float ay = std::cos(pitch) * std::sin(roll);
  const float az = std::cos(pitch) * std::cos(roll);
  imuSetAccel(ax, ay, az);
}

}  // namespace Furble::Sim
