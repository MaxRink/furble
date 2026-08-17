#ifndef FURBLE_UI_GESTURE_H
#define FURBLE_UI_GESTURE_H

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

  /** Clear all detector state. */
  void reset(void);

 private:
  static constexpr float SHAKE_THRESHOLD = 0.6f;
  static constexpr float TAP_THRESHOLD = 1.5f;
  static constexpr float TAP_RELEASE_THRESHOLD = 0.5f;
  static constexpr uint32_t SHAKE_SAMPLES = 3;
  static constexpr uint32_t TAP_WINDOW_MS = 120;
  static constexpr uint32_t DOUBLE_TAP_MIN_MS = 80;
  static constexpr uint32_t DOUBLE_TAP_MAX_MS = 400;
  static constexpr uint32_t REFRACTORY_MS = 750;

  static bool elapsed(uint32_t now, uint32_t start, uint32_t duration);
  void recordGesture(uint32_t now);

  bool m_Ready = false;
  float m_LastMagnitude = 0.0f;
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
