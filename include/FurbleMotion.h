#ifndef FURBLE_MOTION_H
#define FURBLE_MOTION_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Furble::Motion {

/**
 * Pure stationary detector over accelerometer magnitude samples.
 *
 * Entry is slow and exit is immediate. The rolling window must stay under the
 * variance threshold for STATIONARY_MS before the state flips to stationary,
 * while a single sample far from the running mean flips it straight back to
 * moving. A sample more than 0.141 g off the running mean is reported within
 * one sample period; a gentler start takes until the window variance crosses.
 *
 * The ceiling of a magnitude-variance detector: it cannot see constant
 * acceleration or slow rotation at all, because neither changes the magnitude.
 * A vehicle cruising at a steady speed reads stationary. Any receiver policy
 * built on this has to survive that, which is why plan 15 keeps a bounded
 * cached-fix lifetime rather than trusting the detector.
 *
 * The class holds no hardware or timer dependency so the thresholds, the
 * hysteresis and the window are covered by tests/host/motion_detector_test.cpp.
 */
class Detector {
 public:
  /** Sample period the caller is expected to feed, in milliseconds. */
  static constexpr uint16_t SAMPLE_MS = 100;
  /** Rolling window length, five seconds at SAMPLE_MS. */
  static constexpr size_t WINDOW_SAMPLES = 50;
  /** Continuous still time before the state flips to stationary. */
  static constexpr uint32_t STATIONARY_MS = 60 * 1000;
  /**
   * Variance ceiling in squared g.
   *
   * Derived on paper, never measured against a sensor. It has to be checked on
   * the BMI270 and again on the MPU6886 before anything consumes the result.
   */
  static constexpr float VARIANCE_THRESHOLD = 0.02f;

  /** Acceleration magnitude in g for one accelerometer sample. */
  static float magnitude(float x, float y, float z) {
    return std::sqrt((x * x) + (y * y) + (z * z));
  }

  /** Drop the window and the state. The next stationary entry starts over. */
  void reset(void) {
    m_Samples.fill(0.0f);
    m_Count = 0;
    m_Next = 0;
    m_Stationary = false;
    m_Timing = false;
    m_StillSinceMs = 0;
  }

  /**
   * Feed one acceleration magnitude in g taken at now_ms.
   *
   * A non-finite sample is discarded rather than poisoning the window.
   * Returns the stationary state after the sample.
   */
  bool sample(float magnitude, uint64_t now_ms) {
    if (!std::isfinite(magnitude)) {
      return m_Stationary;
    }

    // Compare against the mean before the sample joins the window, so a single
    // jolt is caught on the sample that carries it rather than one later.
    const float deviation = magnitude - mean();
    const bool outlier = (m_Count > 0) && ((deviation * deviation) > VARIANCE_THRESHOLD);

    m_Samples[m_Next] = magnitude;
    m_Next = (m_Next + 1) % WINDOW_SAMPLES;
    if (m_Count < WINDOW_SAMPLES) {
      m_Count++;
    }

    const bool full = (m_Count == WINDOW_SAMPLES);
    if (outlier || (full && (variance() > VARIANCE_THRESHOLD))) {
      m_Stationary = false;
      m_Timing = false;
      m_StillSinceMs = 0;
      return false;
    }

    // A partial window cannot say anything about variance yet.
    if (!full) {
      return m_Stationary;
    }

    if (!m_Timing) {
      m_Timing = true;
      m_StillSinceMs = now_ms;
    } else if ((now_ms - m_StillSinceMs) >= STATIONARY_MS) {
      m_Stationary = true;
    }

    return m_Stationary;
  }

  bool isStationary(void) const { return m_Stationary; }

 private:
  float mean(void) const {
    if (m_Count == 0) {
      return 0.0f;
    }
    float sum = 0.0f;
    for (size_t index = 0; index < m_Count; index++) {
      sum += m_Samples[index];
    }
    return sum / static_cast<float>(m_Count);
  }

  float variance(void) const {
    if (m_Count == 0) {
      return 0.0f;
    }
    const float average = mean();
    float sum = 0.0f;
    for (size_t index = 0; index < m_Count; index++) {
      const float difference = m_Samples[index] - average;
      sum += difference * difference;
    }
    return sum / static_cast<float>(m_Count);
  }

  std::array<float, WINDOW_SAMPLES> m_Samples {};
  size_t m_Count = 0;
  size_t m_Next = 0;
  bool m_Stationary = false;
  bool m_Timing = false;
  uint64_t m_StillSinceMs = 0;
};

}  // namespace Furble::Motion

#endif
