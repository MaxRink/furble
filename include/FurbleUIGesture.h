#ifndef FURBLE_UI_GESTURE_H
#define FURBLE_UI_GESTURE_H

#include <atomic>
#include <cstdint>

namespace Furble {

class GestureDetector {
 public:
  enum class gesture_t {
    TAP,
    SHAKE,
    DOUBLE_TAP,
  };

  /** Read one accelerometer sample and report at most one gesture. */
  bool poll(bool doubleTap, gesture_t &gesture);

  /**
   * Classify one accelerometer sample at an explicit time.
   *
   * This is the shared, deterministic seam used by the simulator and host
   * tests. The hardware-facing poll() method only acquires a sample and
   * delegates here, so protocol and gesture state-machine coverage does not
   * require an IMU attached.
   */
  bool sample(float x, float y, float z, uint32_t now, bool doubleTap, gesture_t &gesture);

  /** Clear all detector state. */
  void reset(void);

  /**
   * Amplitude calibration scale applied to the tap and shake thresholds.
   *
   * A real sensor is never the paper ideal: the BMI270 and the MPU6886 differ
   * in noise floor and full scale defaults, and a cased device damps an
   * impulse differently from a bare board. The per-type gain below is the
   * shipped starting point; this scale is the runtime knob the console uses to
   * tune it against a specific board without a reflash. 1.0 is the default.
   */
  static void setScale(float scale);
  static float getScale(void);

 private:
  static constexpr float SHAKE_THRESHOLD = 0.6f;
  static constexpr float TAP_THRESHOLD = 1.5f;
  static constexpr float TAP_RELEASE_THRESHOLD = 0.5f;
  static constexpr uint32_t SHAKE_SAMPLES = 3;
  static constexpr uint32_t TAP_WINDOW_MS = 120;
  static constexpr uint32_t DOUBLE_TAP_MIN_MS = 80;
  static constexpr uint32_t DOUBLE_TAP_MAX_MS = 400;
  static constexpr uint32_t REFRACTORY_MS = 750;

  /** Per-sensor amplitude gain, read from the live IMU on the hardware path. */
  static float typeGain(void);

  static bool elapsed(uint32_t now, uint32_t start, uint32_t duration);
  void recordGesture(uint32_t now);

  static std::atomic<float> s_Scale;

  float m_TypeGain = 1.0f;
  bool m_Ready = false;
  float m_BaselineMagnitude = 1.0f;
  float m_ShakeEwma = 0.0f;
  uint8_t m_ShakeSamples = 0;
  bool m_ShakeReported = false;
  bool m_TapCandidate = false;
  uint32_t m_TapStarted = 0;
  bool m_PendingTap = false;
  uint32_t m_PendingTapAt = 0;
  bool m_HasGesture = false;
  uint32_t m_LastGestureAt = 0;
};

}  // namespace Furble

#endif
