#include <algorithm>
#include <cmath>

#include <M5Unified.h>
#include <esp_timer.h>

#include "FurbleUIGesture.h"
#if defined(FURBLE_SIM)
#include "driver.h"
#endif

namespace Furble {

std::atomic<float> GestureDetector::s_Scale {1.0f};

void GestureDetector::setScale(float scale) {
  // Clamp to a range that still leaves a usable detector at either end. A zero
  // or negative scale would fire on sensor noise; a huge one is a dead switch.
  if (!std::isfinite(scale)) {
    return;
  }
  s_Scale.store(std::clamp(scale, 0.25f, 4.0f), std::memory_order_relaxed);
}

float GestureDetector::getScale(void) {
  return s_Scale.load(std::memory_order_relaxed);
}

float GestureDetector::typeGain(void) {
#if defined(FURBLE_SIM)
  return 1.0f;
#else
  // The MPU6886 boards read noisier than the BMI270 at the same physical
  // impulse, so they need a higher bar for the same false-positive rate. This
  // is an initial estimate; plans/17 records that it is measured on hardware
  // and the console scale is the knob for a board that still disagrees.
  return (M5.Imu.getType() == m5::imu_mpu6886) ? 1.25f : 1.0f;
#endif
}

bool GestureDetector::elapsed(uint32_t now, uint32_t start, uint32_t duration) {
  return static_cast<uint32_t>(now - start) >= duration;
}

void GestureDetector::reset(void) {
  m_Ready = false;
  m_BaselineMagnitude = 1.0f;
  m_ShakeEwma = 0.0f;
  m_ShakeSamples = 0;
  m_ShakeReported = false;
  m_TapCandidate = false;
  m_TapStarted = 0;
  m_PendingTap = false;
  m_PendingTapAt = 0;
  m_HasGesture = false;
  m_LastGestureAt = 0;
}

void GestureDetector::recordGesture(uint32_t now) {
  m_HasGesture = true;
  m_LastGestureAt = now;
  m_TapCandidate = false;
  m_PendingTap = false;
}

bool GestureDetector::poll(bool doubleTap, gesture_t &gesture) {
#if defined(FURBLE_SIM)
  if (!Furble::Sim::imuEnabled()) {
    return false;
  }

  Furble::Sim::imuUpdate();
  float accel[3];
  if (!Furble::Sim::imuGetAccel(&accel[0], &accel[1], &accel[2])) {
    return false;
  }
#else
  if (!M5.Imu.isEnabled()) {
    return false;
  }

  M5.Imu.update();
  float accel[3];
  if (!M5.Imu.getAccel(&accel[0], &accel[1], &accel[2])) {
    return false;
  }
#endif

  m_TypeGain = typeGain();
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  return sample(accel[0], accel[1], accel[2], now, doubleTap, gesture);
}

bool GestureDetector::sample(float x,
                             float y,
                             float z,
                             uint32_t now,
                             bool doubleTap,
                             gesture_t &gesture) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    return false;
  }

  const float magnitude = std::sqrt((x * x) + (y * y) + (z * z));
  // Reject impossible/overflowed host or sensor values without allowing them
  // to poison the baseline or create a false shutter event.
  if (!std::isfinite(magnitude) || (magnitude > 16.0f)) {
    return false;
  }

  if (!m_Ready) {
    m_BaselineMagnitude = magnitude;
    m_ShakeEwma = 0.0f;
    m_Ready = true;
    return false;
  }

  const bool inRefractory = m_HasGesture && !elapsed(now, m_LastGestureAt, REFRACTORY_MS);

  if (m_PendingTap && (static_cast<uint32_t>(now - m_PendingTapAt) > DOUBLE_TAP_MAX_MS)) {
    m_PendingTap = false;
    if (doubleTap && !inRefractory) {
      recordGesture(now);
      gesture = gesture_t::TAP;
      return true;
    }
  }

  const float gain = m_TypeGain * s_Scale.load(std::memory_order_relaxed);
  const float shakeThreshold = SHAKE_THRESHOLD * gain;
  const float tapThreshold = TAP_THRESHOLD * gain;
  const float tapReleaseThreshold = TAP_RELEASE_THRESHOLD * gain;

  const float deviation = std::fabs(magnitude - m_BaselineMagnitude);
  m_ShakeEwma = (0.5f * deviation) + (0.5f * m_ShakeEwma);
  if (deviation > shakeThreshold) {
    m_ShakeSamples = std::min<uint8_t>(m_ShakeSamples + 1, SHAKE_SAMPLES);
  } else {
    m_ShakeSamples = 0;
    m_ShakeReported = false;
  }

  if ((m_ShakeEwma > shakeThreshold) && (m_ShakeSamples >= SHAKE_SAMPLES) && !m_ShakeReported) {
    m_ShakeReported = true;
    m_TapCandidate = false;
    m_PendingTap = false;
    if (!inRefractory) {
      recordGesture(now);
      gesture = gesture_t::SHAKE;
      return true;
    }
  }

  if (!m_TapCandidate && !inRefractory && (deviation >= tapThreshold)) {
    m_TapCandidate = true;
    m_TapStarted = now;
  }

  if (m_TapCandidate) {
    if (elapsed(now, m_TapStarted, TAP_WINDOW_MS)) {
      m_TapCandidate = false;
    } else if (deviation <= tapReleaseThreshold) {
      m_TapCandidate = false;
      if (!doubleTap) {
        recordGesture(now);
        gesture = gesture_t::TAP;
        return true;
      }

      if (m_PendingTap) {
        const uint32_t gap = static_cast<uint32_t>(now - m_PendingTapAt);
        if ((gap >= DOUBLE_TAP_MIN_MS) && (gap <= DOUBLE_TAP_MAX_MS)) {
          recordGesture(now);
          gesture = gesture_t::DOUBLE_TAP;
          return true;
        }
      }

      m_PendingTap = true;
      m_PendingTapAt = now;
    }
  }

  // Follow slow changes in the measured gravity magnitude only while the
  // signal is quiet. This preserves tap thresholds across sensor calibration
  // differences without teaching the baseline an impulse.
  if (!m_TapCandidate && !m_PendingTap && (deviation < tapReleaseThreshold)) {
    m_BaselineMagnitude = (0.95f * m_BaselineMagnitude) + (0.05f * magnitude);
  }

  return false;
}

}  // namespace Furble
