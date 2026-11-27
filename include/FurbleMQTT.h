#ifndef FURBLE_MQTT_H
#define FURBLE_MQTT_H

// FURBLE_MQTT defaults off. When disabled the whole class and its esp-mqtt
// dependency drop out, so nothing here (including <mqtt_client.h>) is pulled in.
#if defined(FURBLE_MQTT) && FURBLE_MQTT

#if defined(FURBLE_MQTT_HOST_TEST)
#include "mqtt_host_dependencies.h"
#else
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include "FurbleControl.h"
#include "FurbleOTACommand.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "interval.h"
#endif

#if defined(FURBLE_MQTT_HOST_TEST)
#include "FurbleOTACommand.h"
#endif

struct cJSON;

namespace Furble {

/**
 * Heap owner for the MQTT singleton.
 *
 * The singleton is large (about a kilobyte of strings, topic caches and the
 * OTA handler state) and it is created in MQTT::init() during app_main.
 * Keeping it off .bss returns that static DRAM to the boot heap on every MQTT
 * profile. The allocation happens before the owner gate, so the gate measures
 * the same free bytes it measured while the object lived in .bss. The type
 * exists only to reach the private constructor; nothing else constructs it.
 */
struct MQTTInstance;

class MQTT {
 public:
  static MQTT &getInstance(void);

  /**
   * Release the retained event queue and copies block.
   *
   * Runtime teardown keeps them alive so a stopped client can still deliver a
   * late callback. At destruction no callback can arrive, so the retained
   * blocks are released. Without this, moving the singleton to the heap turns
   * them into a host leak-check finding.
   */
  ~MQTT();

  MQTT(MQTT const &) = delete;
  MQTT(MQTT &&) = delete;
  MQTT &operator=(MQTT const &) = delete;
  MQTT &operator=(MQTT &&) = delete;

  /** Start the MQTT service when its master setting is enabled. */
  static void init(void);

  /** Re-read settings and start or stop the broker session. */
  void reloadSetting(void);

  /** Stop the session until the next explicit connect request. */
  void disconnect(void);

  /** Delete retained Home Assistant discovery records. */
  void clearDiscovery(void);

#if defined(FURBLE_SIM_MQTT)
  /** Stop the native simulator transport before simulator tasks are joined. */
  bool shutdownForSimulator(uint32_t timeout_ms);

  /** Simulator observability: the owner task exists. */
  bool simTaskCreated(void) const;
  /** Simulator observability: bounded connection-failure accounting. */
  uint32_t simConnectFailures(void) const;
  /** Simulator observability: the current retry backoff step in milliseconds. */
  uint32_t simRetryDelayMs(void) const;
  /** Simulator observability: the failure classifier stopped the client. */
  bool simBlocked(void) const;
#endif

  bool isConfigured(void) const;
  bool isConnected(void) const { return m_Connected.load(); }

  /**
   * Attach the OTA command handler.
   *
   * The handler is owned by the composition that links the HTTPS transport,
   * not by MQTT. Call this before the broker session starts, or from the owner
   * task; the pointer is read on the owner task only. Without a handler the
   * `cmd/ota` topic still answers, with the documented
   * `transport_unavailable` refusal, so a dashboard can discover the build.
   */
  void attachOTAHandler(OTA::Command::Handler *handler);

  /**
   * Record the boot health verdict the OTA composition evaluated.
   *
   * Ownership stays with the composition; MQTT copies the value so the
   * `state/ota` payload and `ota status` can report a rollback even though the
   * update itself finished on a previous boot.
   */
  void setOTABootReport(const OTA::BootReport &report);

  /**
   * Context the OTA composition builds its handler on.
   *
   * The handler needs battery, control, intervalometer and link observations
   * from this owner task, so the composition passes this reference instead of
   * re-implementing the platform reads. The lifetime is the MQTT singleton.
   */
  OTA::Command::Context &otaContext(void) { return m_OTAContext; }

#if defined(FURBLE_MQTT_HOST_TEST)
  /** Run one production MQTT task iteration without creating a host task. */
  void hostTaskStep(void);
  /** Idle wait in milliseconds, so a test can prove the task period. */
  uint32_t hostTaskWaitMs(void) const;
  /** Whether the pre-task owner gate admitted the owner task. */
  bool hostTaskCreated(void) const { return m_Task != nullptr; }
  /** Connection-failure accounting, retry step and classifier block state. */
  uint32_t hostConnectFailures(void) const;
  uint32_t hostRetryDelayMs(void) const;
  bool hostBlocked(void) const;
#endif

 private:
  MQTT() = default;
  friend struct MQTTInstance;

  static constexpr uint32_t TASK_PERIOD_MS = 1000;
  // A rejected shutter or focus release leaves the camera held, so retry it on
  // this shorter deadline instead of the one-second idle period.
  static constexpr uint32_t RELEASE_RETRY_MS = 250;
  static constexpr uint32_t CAMERAS_PERIOD_MS = 1000;
  static constexpr uint32_t BATTERY_PERIOD_MS = 60 * 1000;
  static constexpr uint32_t GPS_PERIOD_MS = 10 * 1000;
  // A broker or router outage must never silence the hub until a reboot, so a
  // retryable failure always schedules another attempt. The gap doubles from
  // RETRY_INITIAL_MS to RETRY_MAX_MS and then stays there: one attempt a
  // minute is negligible traffic, and it rejoins a returning broker inside a
  // minute. The 300 s WiFi cap would leave a shutter command unanswered for
  // minutes, and the old flat 10 s retry hit the removed ten-failure limit
  // after about 100 s of outage.
  static constexpr uint32_t RETRY_INITIAL_MS = 10 * 1000;
  static constexpr uint32_t RETRY_MAX_MS = 60 * 1000;
  static constexpr uint32_t MAX_HOLD_MS = 60 * 1000;
  static constexpr uint32_t OFFLINE_ACK_TIMEOUT_MS = 250;
  static constexpr size_t EVENT_QUEUE_LENGTH = 4;
  static constexpr size_t MAX_COMMAND_TOPIC_LENGTH = 256;
  static constexpr size_t MAX_COMMAND_PAYLOAD_LENGTH = 2048;

  enum class queued_event_type_t : uint8_t { MQTT };

  struct queued_event_t {
    queued_event_type_t type = queued_event_type_t::MQTT;
    esp_mqtt_client_handle_t client = nullptr;
    int32_t event_id = 0;
    int32_t current_data_offset = 0;
    int32_t total_data_len = 0;
    int32_t topic_len = 0;
    int32_t data_len = 0;
    int32_t error_type = 0;
    int32_t message_id = 0;
    int qos = 0;
    bool retain = false;
    bool dup = false;
    char topic[MAX_COMMAND_TOPIC_LENGTH + 1] = {};
    char data[MAX_COMMAND_PAYLOAD_LENGTH + 1] = {};
  };

  enum class interval_phase_t : uint8_t {
    WAIT,
    SHUTTER,
  };

  static void taskEntry(void *param);
  static void eventHandler(void *handler_arg,
                           esp_event_base_t event_base,
                           int32_t event_id,
                           void *event_data);
  static void holdTimerCallback(void *arg);
  static void intervalTimerCallback(void *arg);

  void task(void);
  void taskStep(void);
  TickType_t taskWaitTicks(void) const;
  bool enqueueEvent(const queued_event_t &event);
  void processLifecycleEvents(void);
  void clearQueuedEvents(void);
  void processEvent(const queued_event_t &event);
  void processDueTimers(void);
  void servicePendingReleases(void);
  /**
   * Handle a connected or disconnected event already matched to m_Client. The
   * error fields decide whether a disconnect is retryable or permanent.
   */
  void handleLifecycleEvent(int32_t event_id, int32_t error_type, int32_t connect_return_code);
  void handleEvent(const queued_event_t &event);
  void handleData(const queued_event_t &event);
  void handleCommand(const std::string &topic, const std::string &payload);
  void handleLocation(const std::string &payload);
  /** Route one `cmd/ota` delivery to the attached handler and report it. */
  void handleOTA(const queued_event_t &event);
  /**
   * Publish `state/ota` when the handler revision or payload changed, and keep
   * the shared status store current for the console.
   */
  void publishOTAStatus(void);
  void publishOTAUnavailable(void);

  bool networkReady(void) const;
  /** Internal storage the next client start must allocate beyond its floor. */
  struct start_extra_t {
    size_t bytes;
    size_t largestBlock;
  };
  start_extra_t startExtra(void) const;
  bool startClient(void);
  void stopClient(void);
  void loadTopics(void);
  /** True when no backoff deadline holds the next start attempt back. */
  bool retryDue(void) const;
  /** Count one retryable failure, grow the backoff and schedule the next try. */
  void scheduleRetry(void);
  /** A live session starts the retry policy over. */
  void clearRetry(void);
  /** The current backoff step in milliseconds, zero when none is pending. */
  uint32_t retryDelayMs(void) const;

  bool publish(const std::string &topic,
               const std::string &payload,
               int qos,
               bool retain,
               int *message_id = nullptr);
  void publishError(const std::string &message);
  void publishState(bool force);
  void publishCameras(bool force);
  void publishBattery(bool force);
  void publishGPS(bool force);
  void publishInterval(bool force);
  void publishShutter(const std::string &state);

  std::string makeCamerasPayload(const std::vector<std::shared_ptr<Camera>> &cameras) const;
  std::string makeCameraStatePayload(const Control::target_status_t &status,
                                     const Camera *camera) const;
  std::string makeBatteryPayload(const Platform::battery_t &battery) const;
  std::string makeGPSPayload(void) const;
  std::string makeIntervalPayload(void) const;

  void publishDiscovery(void);
  void publishDiscoveryDevice(const std::string &topic,
                              const std::string &device_id,
                              const std::string &name,
                              const std::string &via_device,
                              cJSON *components);
  void clearDiscoveryRecords(void);

  bool sendCommand(Control::cmd_t command);
  bool sendHold(uint32_t duration_ms);
  void releaseHold(void);
  void releaseOwnedActuators(void);
  void startInterval(void);
  void stopInterval(void);
  void intervalStep(void);
  void scheduleInterval(uint32_t delay_ms);

  static std::string jsonString(const void *object);
  static std::string trimTopic(const std::string &topic);
  static std::string typeName(Camera::Type type);

  mutable std::mutex m_Mutex;
  mutable std::mutex m_LifecycleMutex;
  TaskHandle_t m_Task = nullptr;
  QueueHandle_t m_EventQueue = nullptr;
#if defined(CONFIG_SPIRAM) && defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) \
    && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  bool m_EventQueueWithCaps = false;
#endif
  // eventHandler builds each event here, not on the 3 KiB esp-mqtt client
  // stack. Allocated with the queue and never freed, because a stopped client
  // may still deliver a late callback; the mutex covers overlapping clients.
  std::mutex m_IncomingMutex;
  queued_event_t *m_Incoming = nullptr;
  // The owner receives each queued event here, not into a stack local. Owned
  // by the owner task alone; allocated in the same block as m_Incoming.
  queued_event_t *m_Received = nullptr;
  esp_mqtt_client_handle_t m_Client = nullptr;
  esp_timer_handle_t m_HoldTimer = nullptr;
  esp_timer_handle_t m_IntervalTimer = nullptr;

  std::atomic<bool> m_Connected = false;
#if defined(FURBLE_SIM_MQTT)
  std::condition_variable m_SimulatorShutdownCondition;
  std::atomic<bool> m_SimulatorShutdownRequested = false;
  std::atomic<bool> m_SimulatorShutdownDeadlineReached = false;
  std::atomic<bool> m_SimulatorShutdownComplete = true;
#endif
  bool m_LifecyclePending = false;
  esp_mqtt_client_handle_t m_LifecycleClient = nullptr;
  int32_t m_LifecycleEvent = 0;
  // The esp-mqtt error classification that came with the lifecycle event. It is
  // copied because the classifier reads it on the owner task, after the client
  // task has returned from the callback and its event object is gone.
  int32_t m_LifecycleErrorType = 0;
  int32_t m_LifecycleConnectReturnCode = 0;
  bool m_Reload = false;
  bool m_ManualDisconnect = false;
  bool m_StopClient = false;
  bool m_Blocked = false;
  bool m_ClearDiscovery = false;
  uint32_t m_ConnectFailures = 0;
  // Capped exponential backoff. m_RetryDelayMs is the current step, zero when
  // no retry is pending; m_NextAttemptMs is the earliest next start attempt,
  // zero when the next one may run immediately. Both are guarded by m_Mutex.
  uint32_t m_RetryDelayMs = 0;
  uint64_t m_NextAttemptMs = 0;
  mutable uint32_t m_LastNetworkLogMs = 0;

  bool m_IntervalRunning = false;
  interval_phase_t m_IntervalPhase = interval_phase_t::WAIT;
  uint32_t m_IntervalRemaining = 0;
  uint32_t m_IntervalTotal = 0;
  uint64_t m_IntervalNextMs = 0;

  bool m_HoldActive = false;
  uint64_t m_HoldDeadlineMs = 0;
  bool m_ShutterOwned = false;
  bool m_FocusOwned = false;
  bool m_ShutterReleasePending = false;
  bool m_FocusReleasePending = false;
  bool m_OfflineStopPending = false;
  bool m_OfflineAcked = false;
  int m_OfflineMessageId = -1;
  uint64_t m_OfflineDeadlineMs = 0;
  bool m_HaveExternalLocation = false;

  uint64_t m_LastBatteryMs = 0;
  uint64_t m_LastBatterySampleMs = 0;
  uint64_t m_LastCamerasSampleMs = 0;
  uint64_t m_LastGPSMs = 0;
  bool m_HaveBatterySample = false;
  bool m_LastCharging = false;
  std::string m_LastCamerasPayload;
  std::string m_LastGPSPayload;
  std::string m_LastIntervalPayload;
  std::vector<std::string> m_DiscoveredCameraIDs;

  // Settings are read once in reloadSetting() and reused by the 1 Hz task.
  // The task used to load five NVS keys on every iteration (P-030). MQTT_HA is
  // cached too, so the connected event and the HA status replay do not touch
  // NVS from the owner task.
  std::atomic<bool> m_HomeAssistantCached = false;
  bool m_EnabledCached = false;
  std::string m_BaseSetting;
  std::string m_URISetting;
  std::string m_UserSetting;
  std::string m_PasswordSetting;
  std::string m_Base;
  std::string m_ID;
  std::string m_URI;
  std::string m_User;
  std::string m_Password;
  std::string m_RootTopic;
  std::string m_StatusTopic;
  std::string m_CamerasTopic;
  std::string m_BatteryTopic;
  std::string m_GPSTopic;
  std::string m_IntervalTopic;
  std::string m_ShutterTopic;
  std::string m_ErrorTopic;
  std::string m_ShutterCommandTopic;
  std::string m_FocusCommandTopic;
  std::string m_IntervalCommandTopic;
  std::string m_ConnectCommandTopic;
  std::string m_DisconnectCommandTopic;
  std::string m_LocationCommandTopic;
  std::string m_OTACommandTopic;
  std::string m_OTAStatusTopic;
  std::string m_HomeAssistantTopic;

  /**
   * Runtime observations for the OTA preconditions.
   *
   * A nested class can read the owner's private connection state, so the
   * battery, control, intervalometer and link answers all come from the same
   * task snapshot. The values are read only when a command needs them.
   */
  class OTAContext final: public OTA::Command::Context {
   public:
    explicit OTAContext(MQTT &owner) : m_Owner(owner) {}

    OTA::Conditions conditions(void) override;
    const char *installedVersion(void) override;
    OTA::BootReport bootReport(void) override;

   private:
    MQTT &m_Owner;
  };

  OTAContext m_OTAContext {*this};
  OTA::Command::Handler *m_OTAHandler = nullptr;
  OTA::BootReport m_OTABootReport {};
  std::string m_OTAStatusPayload;
  uint32_t m_OTAStatusRevision = 0;
};

}  // namespace Furble

#endif  // defined(FURBLE_MQTT) && FURBLE_MQTT

#endif  // FURBLE_MQTT_H
