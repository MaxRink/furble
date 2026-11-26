// Virtual motion engines for the host build.
//
// The BMI270 and MPU6886 engines are register programming against a physical
// I2C bus, which the simulator does not have. What a scenario needs to exercise
// is not the registers but the event semantics: any-motion raises MOVING once a
// sample crosses the chip's slope threshold, no-motion raises STATIONARY after a
// continuous quiet window. This backend models exactly that, reading the
// injected accelerometer through Sim::imuGetAccel, the same surface the
// software backend and the spirit level read. The decision itself lives in
// sim/imu_motion_model.h so a host test drives the same code, and the register
// encoding is covered by tests/host/imu_motion_encoding_test.cpp.
//
// The slope thresholds come from the counts the firmware writes, converted by
// FurbleIMUMotionScale.h. Do not hand-convert them here: a g value that drifts
// from the driver certifies a sensitivity the hardware does not have.

#include "FurbleIMU.h"

#include <mutex>

#include <esp_timer.h>

#include "FurblePlatform.h"
#include "driver.h"
#include "imu_motion_model.h"

namespace Furble {
namespace IMU {

namespace {

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

class VirtualMotionBackend final: public MotionBackend {
 public:
  VirtualMotionBackend(Backend backend,
                       const char *name,
                       const Sim::ImuMotionModel::config_t &config)
      : m_Backend(backend), m_Name(name) {
    m_Model.configure(config);
  }

  bool arm(void) override {
    // Same contract as the firmware backends: the bus lock is held across the
    // sensor access, arm() included.
    std::lock_guard<imu_mutex_t> lock(g_IMUMutex);

    if (!Sim::imuEnabled()) {
      return false;
    }

    m_Model.arm();
    m_InterruptCount = 0;
    m_UsesInterrupt = Platform::getInstance().armMotionWake();
    m_Armed = true;
    return true;
  }

  void disarm(void) override {
    if (!m_Armed) {
      return;
    }
    if (m_UsesInterrupt) {
      Platform::getInstance().disarmMotionWake();
    }
    m_UsesInterrupt = false;
    m_Armed = false;
  }

  bool poll(MotionState &state) override {
    // The firmware backends hold this for their register sequences. Taking it
    // here too keeps the simulator honest about the ownership contract.
    std::lock_guard<imu_mutex_t> lock(g_IMUMutex);

    float accel[3] = {};
    if (!Sim::imuGetAccel(&accel[0], &accel[1], &accel[2])) {
      return false;
    }

    if (!m_Model.step(nowMs(), accel)) {
      return false;
    }

    m_InterruptCount++;
    state = m_Model.moving() ? MotionState::MOVING : MotionState::STATIONARY;
    return true;
  }

  MotionState state(void) const override {
    return m_Model.moving() ? MotionState::MOVING : MotionState::STATIONARY;
  }
  Backend backend(void) const override { return m_Backend; }
  const char *name(void) const override { return m_Name; }
  bool usesInterrupt(void) const override { return m_UsesInterrupt; }
  uint32_t interruptCount(void) const override { return m_InterruptCount; }

 private:
  const Backend m_Backend;
  const char *const m_Name;
  Sim::ImuMotionModel m_Model;
  uint32_t m_InterruptCount = 0;
  bool m_UsesInterrupt = false;
  bool m_Armed = false;
};

}  // namespace

std::unique_ptr<MotionBackend> createBMI270Backend(void) {
  if (Sim::imuChip() != Sim::imu_chip_t::BMI270) {
    return nullptr;
  }
  return std::make_unique<VirtualMotionBackend>(Backend::BMI270, "bmi270-motion",
                                                Sim::bmi270MotionConfig());
}

std::unique_ptr<MotionBackend> createMPU6886Backend(void) {
  if (Sim::imuChip() != Sim::imu_chip_t::MPU6886) {
    return nullptr;
  }
  return std::make_unique<VirtualMotionBackend>(Backend::MPU6886, "mpu6886-wom",
                                                Sim::mpu6886MotionConfig());
}

}  // namespace IMU
}  // namespace Furble
