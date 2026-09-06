// Virtual motion engines for the host build.
//
// The BMI270 and MPU6886 engines are register programming against a physical
// I2C bus, which the simulator does not have. What a scenario needs to exercise
// is not the registers but the event semantics: any-motion raises MOVING once a
// sample crosses the chip's slope threshold, no-motion raises STATIONARY after a
// continuous quiet window. This backend models exactly that, reading the
// injected accelerometer through Sim::imuGetAccel, the same surface the
// software backend and the spirit level read. The register encoding itself is
// covered by tests/host/imu_motion_encoding_test.cpp.

#include "FurbleIMU.h"

#include <cmath>
#include <mutex>

#include <esp_timer.h>

#include "FurblePlatform.h"
#include "driver.h"

namespace Furble {
namespace IMU {

namespace {

// The no-motion window every backend shares. The BMI270 no-motion duration, the
// MPU6886 software hold and the software backend all use this same 60 s.
constexpr uint32_t STATIONARY_HOLD_MS = 60 * 1000;

// Slope thresholds in g, converted from the counts the firmware writes.
// BMI270 any-motion is 0xAA counts at 0.48 mg. MPU6886 wake on motion is 0x10
// counts at 4 mg.
constexpr float BMI270_THRESHOLD_G = 0.0816f;
constexpr float MPU6886_THRESHOLD_G = 0.064f;

// The BMI270 any-motion duration is one count of 20 ms and the MPU6886 raises
// its interrupt on the first qualifying sample, so both engines need exactly
// one sample above the threshold to leave the stationary state.
constexpr uint32_t MOTION_SAMPLES = 1;

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

class VirtualMotionBackend final: public MotionBackend {
 public:
  VirtualMotionBackend(Backend backend, const char *name, float threshold)
      : m_Backend(backend), m_Name(name), m_Threshold(threshold) {}

  bool arm(void) override {
    if (!Sim::imuEnabled()) {
      return false;
    }

    m_State = MotionState::MOVING;
    m_QuietSince = 0;
    m_MotionSamples = 0;
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
    std::lock_guard<std::mutex> lock(g_IMUMutex);

    float accel[3] = {};
    if (!Sim::imuGetAccel(&accel[0], &accel[1], &accel[2])) {
      return false;
    }

    const float magnitude =
        std::sqrt((accel[0] * accel[0]) + (accel[1] * accel[1]) + (accel[2] * accel[2]));
    const uint32_t now = nowMs();

    // The chip thresholds are fixed in hardware, but the scale knob still
    // applies here so a scenario can prove the calibration reaches both paths.
    if (std::fabs(magnitude - 1.0f) >= (m_Threshold * MotionSource::getScale())) {
      m_QuietSince = 0;
      if (m_MotionSamples < MOTION_SAMPLES) {
        m_MotionSamples++;
      }
      if ((m_State == MotionState::MOVING) || (m_MotionSamples < MOTION_SAMPLES)) {
        return false;
      }
      // Any-motion fired. Exactly one engine is armed at a time, so the pair
      // swaps here the same way the BMI270 backend swaps its feature bits.
      m_InterruptCount++;
      m_State = MotionState::MOVING;
      state = m_State;
      return true;
    }

    m_MotionSamples = 0;
    if (m_QuietSince == 0) {
      m_QuietSince = (now == 0) ? 1 : now;
      return false;
    }

    if ((m_State == MotionState::MOVING) && ((now - m_QuietSince) >= STATIONARY_HOLD_MS)) {
      // No-motion fired.
      m_InterruptCount++;
      m_State = MotionState::STATIONARY;
      state = m_State;
      return true;
    }

    return false;
  }

  MotionState state(void) const override { return m_State; }
  Backend backend(void) const override { return m_Backend; }
  const char *name(void) const override { return m_Name; }
  bool usesInterrupt(void) const override { return m_UsesInterrupt; }
  uint32_t interruptCount(void) const override { return m_InterruptCount; }

 private:
  const Backend m_Backend;
  const char *const m_Name;
  const float m_Threshold;
  MotionState m_State = MotionState::MOVING;
  uint32_t m_QuietSince = 0;
  uint32_t m_MotionSamples = 0;
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
                                                BMI270_THRESHOLD_G);
}

std::unique_ptr<MotionBackend> createMPU6886Backend(void) {
  if (Sim::imuChip() != Sim::imu_chip_t::MPU6886) {
    return nullptr;
  }
  return std::make_unique<VirtualMotionBackend>(Backend::MPU6886, "mpu6886-wom",
                                                MPU6886_THRESHOLD_G);
}

}  // namespace IMU
}  // namespace Furble
