#ifndef FURBLE_IMU_H
#define FURBLE_IMU_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Furble {

/**
 * Serializes M5.Imu transactions between the UI timers, the debug console
 * probes and the motion engines.
 *
 * Every one of those talks to the same internal I2C bus, and the engines do
 * read-modify-write sequences on the chip's feature and interrupt registers. An
 * interleaved transaction from the spirit level or the IMU live page corrupts
 * one. Hold this for the whole sequence, never across a delay.
 */
extern std::mutex g_IMUMutex;

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

/** Maximum simultaneous motion consumers. */
constexpr size_t MAX_CALLBACKS = 4;

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

  /**
   * Subscribe to motion state changes.
   *
   * There is more than one consumer: the panel wake here, the wake gesture in
   * PR45 and the motion-adaptive GPS policy in PR65. A single callback slot
   * would let whichever ran last silently unsubscribe the others.
   *
   * Callbacks run on whichever task calls poll(), which is the UI task. They
   * must not block and must not call back into MotionSource. Add and remove
   * from that same task.
   *
   * @return false when the registry is full or the pair is already registered.
   */
  bool addCallback(event_callback_t callback, void *context);

  /** Unsubscribe a callback and context pair registered by addCallback(). */
  bool removeCallback(event_callback_t callback, void *context);

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

  /** Republish the active backend's identity into the reader-facing atomics. */
  void publish(void);

  struct subscriber_t {
    event_callback_t callback;
    void *context;
  };

  static std::atomic<float> s_Scale;

  // m_Backend and the subscriber list are owned by the task that calls arm(),
  // disarm() and poll(). The diagnostics timer and the simulator queries read
  // from the UI task, so every value they can observe is mirrored into an
  // atomic rather than reached through the pointer.
  std::unique_ptr<MotionBackend> m_Backend;
  std::array<subscriber_t, MAX_CALLBACKS> m_Subscribers = {};
  size_t m_SubscriberCount = 0;
  std::atomic<MotionState> m_State {MotionState::MOVING};
  std::atomic<Backend> m_BackendId {Backend::NONE};
  std::atomic<const char *> m_BackendName {"none"};
  std::atomic<bool> m_UsesInterrupt {false};
  std::atomic<uint32_t> m_InterruptCount {0};
  std::atomic<bool> m_Armed {false};
  bool m_Initialized = false;
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
