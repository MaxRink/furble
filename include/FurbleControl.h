#ifndef FURBLE_CONTROL_H
#define FURBLE_CONTROL_H

#include <cstddef>
#include <memory>
#include <mutex>

#include <Camera.h>

namespace Furble {

class Control {
 public:
  typedef enum {
    CMD_SHUTTER_PRESS,
    CMD_SHUTTER_RELEASE,
    CMD_FOCUS_PRESS,
    CMD_FOCUS_RELEASE,
    CMD_GPS_UPDATE,
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_ERROR
  } cmd_t;

  typedef enum {
    /** No connections, waiting. */
    STATE_IDLE,
    /** Initiate connections. */
    STATE_CONNECT,
    /** Connections in progress. */
    STATE_CONNECTING,
    /** Initial connection attempt failed. */
    STATE_CONNECT_FAILED,
    /** All connections active. */
    STATE_ACTIVE,
    /** Disconnecting. */
    STATE_DISCONNECTING,
  } state_t;

  class Target {
    friend class Control;

   public:
    Target(Camera *camera);
    ~Target();

    Camera *getCamera(void) const;
    cmd_t getCommand(void);
    void sendCommand(cmd_t cmd);
    void updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync);

    void task(void);

   protected:
    volatile bool m_Stopped = false;

   private:
    static constexpr UBaseType_t m_QueueLength = 8;

    QueueHandle_t m_Queue = NULL;
    Furble::Camera *m_Camera = NULL;
    Camera::gps_t m_GPS;
    Camera::timesync_t m_Timesync;
    float m_RssiAverage = 0.0f;
    bool m_HasRssi = false;
  };

  static Control &getInstance();

  Control(Control const &) = delete;
  Control(Control &&) = delete;
  Control &operator=(Control const &) = delete;
  Control &operator=(Control &&) = delete;

  const uint32_t TIMEOUT_DEFAULT_MS = (30 * 1000);
  const uint32_t TIMEOUT_INFINITE_MS = (5 * 1000);
  const uint32_t SLEEP_INFINITE_MS = (5 * 1000);
  const uint32_t BACKOFF_MAX_MS = (120 * 1000);
  const uint32_t BACKOFF_SLICE_MS = 100;

  /**
   * FreeRTOS control task function.
   */
  void task(void);

  /**
   * Send control command to active connections.
   */
  BaseType_t sendCommand(cmd_t cmd);

  /**
   * Update GPS and timesync values.
   */
  BaseType_t updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync);

  /**
   * Are all active cameras still connected?
   */
  bool allConnected(void);

  /**
   * Get a snapshot of the active targets.
   *
   * Copied under the mutex so callers never iterate the live vector while
   * disconnect() clears it. The pointers stay owned by Control.
   */
  std::vector<Control::Target *> getTargets(void);

  /**
   * Connect to all active cameras.
   */
  void connectAll(bool infiniteReconnect);

  /**
   * Disconnect all connected cameras.
   */
  void disconnect(void);

  /**
   * Add specified camera to active target list.
   */
  void addActive(Camera *camera);

  /**
   * Get current camera connection attempt.
   *
   * @return Camera being connected otherwise nullptr.
   */
  Camera *getConnectingCamera(void) const;

  /** Retrieve current control state. */
  state_t getState(void) const;

  /** Retrieve the number of active camera targets. */
  size_t getTargetCount(void) const;

  /** Retrieve the number of connected camera targets. */
  size_t getConnectedTargetCount(void) const;

  /** Set the maximum transmit power and reset the adaptive level. */
  void setPower(esp_power_level_t power);

  /** Enable or disable adaptive connection parameters on active cameras. */
  void setConnSaver(bool enabled);

 private:
  Control() {};

  /** Iterate over cameras and attempt connection. */
  state_t connectAll(void);

  /**
   * Move to a new state and update the light sleep lock to match.
   *
   * Every state change goes through here, that is what keeps the lock balanced.
   */
  void setState(state_t state);

  /**
   * Sample connection RSSI and adjust the shared transmit power.
   *
   * Control task only. Takes m_Mutex internally in short sections. NVS reads,
   * per-camera RSSI reads and radio calls all run with the mutex released.
   */
  void sampleAdaptivePower(void);

  /**
   * Reset adaptive power tracking state. No NVS or radio calls.
   *
   * Caller must hold m_Mutex.
   */
  void resetAdaptiveState(void);

  /**
   * Request one shared connection transmit power level from the radio.
   *
   * Blocking radio call. Caller must not hold m_Mutex.
   */
  void applyPower(esp_power_level_t power);

  static constexpr UBaseType_t m_QueueLength = 32;
  static constexpr const char *POWER_LOCK_OWNER = "control";

  QueueHandle_t m_Queue = NULL;
  mutable std::mutex m_Mutex;
  std::vector<std::unique_ptr<Control::Target>> m_Targets;

  bool m_InfiniteReconnect = false;
  bool m_ReconnectBackoff = false;
  uint32_t m_ReconnectAttempt = 0;
  volatile bool m_ConnectAbort = false;
  volatile bool m_ConnectInProgress = false;
  state_t m_State = STATE_IDLE;

  // setState() runs from the control task and from the UI task
  std::mutex m_StateMutex;
  bool m_SleepLockHeld = false;

  // Camera connects are serialised, the following tracks the last attempt
  Camera *m_ConnectCamera = nullptr;

  // User transmit power cap, loaded from TX_POWER at first getInstance()
  esp_power_level_t m_Power = ESP_PWR_LVL_P3;

  // Adaptive power state, guarded by m_Mutex
  uint32_t m_LastRssiSample = 0;
  uint8_t m_RssiStrongSamples = 0;
  uint8_t m_RssiWeakSamples = 0;
  bool m_AdaptiveActive = false;
  esp_power_level_t m_AdaptivePower = ESP_PWR_LVL_P3;
};

};  // namespace Furble

extern "C" {
void control_task(void *param);
}

struct FurbleCtx {
  Furble::Control *control;
  bool cancelled;
};

#endif
