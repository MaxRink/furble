#include <algorithm>
#include <cmath>

#include <M5Unified.h>
#include <esp_timer.h>

#include "FurbleUIGesture.h"

namespace Furble {

bool GestureDetector::elapsed(uint32_t now, uint32_t start, uint32_t duration) {
  return static_cast<uint32_t>(now - start) >= duration;
}

void GestureDetector::reset(void) {
  m_Ready = false;
  m_LastMagnitude = 0.0f;
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
  if (!M5.Imu.isEnabled()) {
    return false;
  }

  M5.Imu.update();
  float accel[3];
  if (!M5.Imu.getAccel(&accel[0], &accel[1], &accel[2])) {
    return false;
  }

  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  const float magnitude =
      std::sqrt((accel[0] * accel[0]) + (accel[1] * accel[1]) + (accel[2] * accel[2]));

  if (!m_Ready) {
    m_LastMagnitude = magnitude;
    m_ShakeEwma = 0.0f;
    m_Ready = true;
    return false;
  }

  const bool inRefractory = m_HasGesture && !elapsed(now, m_LastGestureAt, REFRACTORY_MS);

  if (m_PendingTap && elapsed(now, m_PendingTapAt, DOUBLE_TAP_MAX_MS)) {
    m_PendingTap = false;
    if (doubleTap && !inRefractory) {
      recordGesture(now);
      gesture = gesture_t::TAP;
      m_LastMagnitude = magnitude;
      return true;
    }
  }

  const float deviation = std::fabs(magnitude - 1.0f);
  m_ShakeEwma = (0.5f * deviation) + (0.5f * m_ShakeEwma);
  if (deviation > SHAKE_THRESHOLD) {
    m_ShakeSamples = std::min<uint8_t>(m_ShakeSamples + 1, SHAKE_SAMPLES);
  } else {
    m_ShakeSamples = 0;
    m_ShakeReported = false;
  }

  if ((m_ShakeEwma > SHAKE_THRESHOLD) && (m_ShakeSamples >= SHAKE_SAMPLES) && !m_ShakeReported) {
    m_ShakeReported = true;
    m_TapCandidate = false;
    m_PendingTap = false;
    if (!inRefractory) {
      recordGesture(now);
      gesture = gesture_t::SHAKE;
      m_LastMagnitude = magnitude;
      return true;
    }
  }

  const float delta = std::fabs(magnitude - m_LastMagnitude);
  m_LastMagnitude = magnitude;

  if (!m_TapCandidate && !inRefractory && (delta > TAP_THRESHOLD)) {
    m_TapCandidate = true;
    m_TapStarted = now;
  }

  if (m_TapCandidate) {
    if (elapsed(now, m_TapStarted, TAP_WINDOW_MS)) {
      m_TapCandidate = false;
    } else if (delta < TAP_RELEASE_THRESHOLD) {
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

  return false;
}

}  // namespace Furble
