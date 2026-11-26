#ifndef FURBLE_SIM_IMU_MOTION_MODEL_H
#define FURBLE_SIM_IMU_MOTION_MODEL_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "FurbleIMUMotionScale.h"

namespace Furble::Sim {

/*
 * Motion engine model for the simulator.
 *
 * The firmware engines are register programming against a bus the simulator does
 * not have. What a scenario needs is the event semantics: any-motion raises
 * moving once a sample crosses the chip's slope threshold, no-motion raises
 * stationary after a continuous quiet window below its own threshold. This
 * class models exactly that and nothing else, with no simulator or ESP
 * dependency, so the same code runs in the simulator and in a host test.
 *
 * The thresholds are the documented ones. FurbleIMUMotionScale.h converts the
 * counts the backends write into g, and sim/FurbleIMUSim.cpp passes those
 * values, so the model cannot certify a sensitivity the hardware does not have.
 *
 * Both engines run in the chip. While the device moves only no-motion is armed,
 * so a slope above the no-motion threshold restarts the quiet window without
 * reporting anything. While it is stationary only any-motion is armed, so a
 * slope above the any-motion threshold reports moving on the first qualifying
 * sample. The two thresholds are not the same number: the BMI270 configures
 * 83.0 mg for any-motion and 70.3 mg for no-motion.
 */

class ImuMotionModel {
 public:
  struct config_t {
    /** Any-motion slope threshold in g, from the backend's configured counts. */
    float anyMotionThresholdG = 0.0f;
    /** No-motion slope threshold in g, from the backend's configured counts. */
    float noMotionThresholdG = 0.0f;
    /** Continuous quiet time below the no-motion threshold before stationary. */
    uint32_t stationaryHoldMs = 0;
    /** Samples above the any-motion threshold before it fires. */
    uint32_t motionSamples = 1;
  };

  /** Start a poll cycle from the moving state, as the backends do on arm(). */
  void arm(void) {
    m_Moving = true;
    m_QuietSince = 0;
    m_MotionSamples = 0;
    m_HaveSample = false;
    m_LastAccel[0] = 0.0f;
    m_LastAccel[1] = 0.0f;
    m_LastAccel[2] = 0.0f;
  }

  void configure(const config_t &config) { m_Config = config; }

  bool moving(void) const { return m_Moving; }

  /**
   * Feed one accelerometer sample.
   *
   * @return true when the reported state changed.
   */
  bool step(uint32_t nowMs, const float accel[3]) {
    if (!m_HaveSample) {
      m_LastAccel[0] = accel[0];
      m_LastAccel[1] = accel[1];
      m_LastAccel[2] = accel[2];
      m_HaveSample = true;
    }
    const float slope = std::max(
        std::fabs(accel[0] - m_LastAccel[0]),
        std::max(std::fabs(accel[1] - m_LastAccel[1]), std::fabs(accel[2] - m_LastAccel[2])));
    m_LastAccel[0] = accel[0];
    m_LastAccel[1] = accel[1];
    m_LastAccel[2] = accel[2];

    if (m_Moving) {
      // Moving: no-motion is the armed engine. Above its threshold the chip
      // restarts its duration counter and reports nothing.
      if (slope >= m_Config.noMotionThresholdG) {
        m_QuietSince = 0;
        return false;
      }
      if (m_QuietSince == 0) {
        // Zero is free as the sentinel because it is the first millisecond
        // after boot, long before any backend is armed.
        m_QuietSince = (nowMs == 0) ? 1 : nowMs;
        return false;
      }
      if ((nowMs - m_QuietSince) >= m_Config.stationaryHoldMs) {
        m_Moving = false;
        m_MotionSamples = 0;
        return true;
      }
      return false;
    }

    // Stationary: any-motion is the armed engine.
    if (slope < m_Config.anyMotionThresholdG) {
      m_MotionSamples = 0;
      return false;
    }
    if (m_MotionSamples < m_Config.motionSamples) {
      m_MotionSamples++;
    }
    if (m_MotionSamples < m_Config.motionSamples) {
      return false;
    }
    m_Moving = true;
    m_QuietSince = 0;
    return true;
  }

 private:
  config_t m_Config;
  bool m_Moving = true;
  uint32_t m_QuietSince = 0;
  uint32_t m_MotionSamples = 0;
  bool m_HaveSample = false;
  float m_LastAccel[3] = {};
};

/**
 * The modelled BMI270 engines' configuration.
 *
 * The counts are the ones the backend writes and the conversion is the
 * documented one, both from FurbleIMUMotionScale.h. The two thresholds stay
 * separate because the chip has two engines: 83.0 mg for any-motion and 70.3 mg
 * for no-motion.
 */
inline ImuMotionModel::config_t bmi270MotionConfig(void) {
  ImuMotionModel::config_t config;
  config.anyMotionThresholdG =
      IMU::Scale::bmi270ThresholdG(IMU::Scale::BMI270_ANY_MOTION_THRESHOLD_COUNTS);
  config.noMotionThresholdG =
      IMU::Scale::bmi270ThresholdG(IMU::Scale::BMI270_NO_MOTION_THRESHOLD_COUNTS);
  config.stationaryHoldMs = IMU::Scale::BMI270_NO_MOTION_DURATION_MS;
  config.motionSamples = IMU::Scale::BMI270_ANY_MOTION_DURATION_COUNTS;
  return config;
}

/**
 * The modelled MPU6886 engine's configuration.
 *
 * That part has no no-motion engine, so its stationary decision is a software
 * hold from the last wake-on-motion event and its quiet threshold is its wake
 * threshold. The 64 mg is an assumption, see the scale header.
 */
inline ImuMotionModel::config_t mpu6886MotionConfig(void) {
  ImuMotionModel::config_t config;
  const float wakeThresholdG =
      IMU::Scale::mpu6886WomThresholdGAssumed(IMU::Scale::MPU6886_WOM_THRESHOLD_COUNTS);
  config.anyMotionThresholdG = wakeThresholdG;
  config.noMotionThresholdG = wakeThresholdG;
  config.stationaryHoldMs = IMU::Scale::MPU6886_STATIONARY_HOLD_MS;
  // The part raises its interrupt on the first sample above the threshold.
  config.motionSamples = 1;
  return config;
}

}  // namespace Furble::Sim

#endif
