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
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "interval.h"
#endif

namespace Furble {

class MQTT {
 public:
  static MQTT &getInstance(void);

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
#endif

  bool isConfigured(void) const;
  bool isConnected(void) const { return m_Connected.load(); }

#if defined(FURBLE_MQTT_HOST_TEST)
  /** Run one production MQTT task iteration without creating a host task. */
  void hostTaskStep(void);
#endif

 private:
  MQTT() = default;

  static constexpr uint32_t TASK_PERIOD_MS = 250;
  static constexpr uint32_t CAMERAS_PERIOD_MS = 1000;
  static constexpr uint32_t BATTERY_PERIOD_MS = 60 * 1000;
  static constexpr uint32_t GPS_PERIOD_MS = 10 * 1000;
  static constexpr uint32_t MAX_CONNECT_FAILURES = 10;
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
  void queueLifecycleEvent(esp_mqtt_client_handle_t client, int32_t event_id);
  void processLifecycleEvents(void);
  void clearQueuedEvents(void);
  void processEvent(const queued_event_t &event);
  void processDueTimers(void);
  void servicePendingReleases(void);
  void handleEvent(const queued_event_t &event);
  void handleData(const queued_event_t &event);
  void handleCommand(const std::string &topic, const std::string &payload);
  void handleLocation(const std::string &payload);

  bool networkReady(void) const;
  bool startClient(void);
  void stopClient(void);
  void loadTopics(void);

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
                              const std::string &components);
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
  bool m_Reload = false;
  bool m_ManualDisconnect = false;
  bool m_StopClient = false;
  bool m_Blocked = false;
  bool m_ClearDiscovery = false;
  uint32_t m_ConnectFailures = 0;
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
  std::string m_HomeAssistantTopic;
};

}  // namespace Furble

#endif  // defined(FURBLE_MQTT) && FURBLE_MQTT

#endif  // FURBLE_MQTT_H
