#ifndef FURBLE_IMU_H
#define FURBLE_IMU_H

#include <atomic>
#include <cstdint>
#include <memory>

namespace Furble {
namespace IMU {

enum class MotionState : uint8_t {
  MOVING,
  STATIONARY,
};

enum class Backend : uint8_t {
  NONE,
  SOFTWARE,
  BMI270,
  MPU6886,
};

using event_callback_t = void (*)(MotionState state, void *context);

class MotionBackend;

/** Motion detector with software and chip-specific hardware backends. */
class MotionSource {
 public:
  static MotionSource &getInstance();

  MotionSource(MotionSource const &) = delete;
  MotionSource(MotionSource &&) = delete;
  MotionSource &operator=(MotionSource const &) = delete;
  MotionSource &operator=(MotionSource &&) = delete;

  ~MotionSource();

  /** Prepare the source. This does not arm an engine. */
  void init(void);

  /** Select and arm the configured motion backend. */
  bool arm(void);

  /** Disable the active backend and release its wake source. */
  void disarm(void);

  /** Poll the active backend and deliver any state change. */
  void poll(void);

  /** Register the callback used for motion state changes. */
  void setCallback(event_callback_t callback, void *context);

  /**
   * Amplitude calibration scale applied to the software backend's slope
   * threshold.
   *
   * Accelerometers vary in noise floor between parts, and a cased device damps
   * differently from a bare board. MOTION_DELTA_G is a shipped starting point;
   * this is the runtime knob the console uses to correct a board that
   * disagrees. Clamped to 0.25 to 4.0: a zero or negative scale would fire on
   * sensor noise, a huge one is a dead switch.
   *
   * The hardware engines threshold in the chip and are not affected.
   */
  static void setScale(float scale);
  static float getScale(void);

  /** The software backend's effective slope threshold in g. */
  static float threshold(void);

  bool isArmed(void) const;
  MotionState state(void) const;
  Backend backend(void) const;
  const char *backendName(void) const;
  bool usesInterrupt(void) const;
  uint32_t interruptCount(void) const;

 private:
  MotionSource() = default;

  void notify(MotionState state);

  static std::atomic<float> s_Scale;

  std::unique_ptr<MotionBackend> m_Backend;
  event_callback_t m_Callback = nullptr;
  void *m_CallbackContext = nullptr;
  MotionState m_State = MotionState::MOVING;
  bool m_Initialized = false;
  bool m_Armed = false;
};

class MotionBackend {
 public:
  virtual ~MotionBackend() = default;

  virtual bool arm(void) = 0;
  virtual void disarm(void) = 0;
  virtual bool poll(MotionState &state) = 0;
  virtual MotionState state(void) const = 0;
  virtual Backend backend(void) const = 0;
  virtual const char *name(void) const = 0;
  virtual bool usesInterrupt(void) const = 0;
  virtual uint32_t interruptCount(void) const = 0;
};

std::unique_ptr<MotionBackend> createSoftwareBackend(void);
std::unique_ptr<MotionBackend> createBMI270Backend(void);
std::unique_ptr<MotionBackend> createMPU6886Backend(void);

}  // namespace IMU
}  // namespace Furble

#endif
