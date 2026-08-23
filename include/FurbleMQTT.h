#ifndef FURBLE_MQTT_H
#define FURBLE_MQTT_H

// FURBLE_MQTT defaults on. When disabled the whole class and its esp-mqtt
// dependency drop out, so nothing here (including <mqtt_client.h>) is pulled in.
#if defined(FURBLE_MQTT) && FURBLE_MQTT

#if defined(FURBLE_MQTT_HOST_TEST)
#include "mqtt_host_dependencies.h"
#else
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

#include "FurbleControl.h"
#include "FurblePlatform.h"
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
  void handleEvent(esp_mqtt_event_handle_t event);
  void handleData(const esp_mqtt_event_handle_t event);
  void handleCommand(const std::string &topic, const std::string &payload);
  void handleLocation(const std::string &payload);

  bool networkReady(void) const;
  bool startClient(void);
  void stopClient(void);
  void loadTopics(void);

  bool publish(const std::string &topic, const std::string &payload, int qos, bool retain);
  void publishError(const std::string &message);
  void publishState(bool force);
  void publishCameras(bool force);
  void publishBattery(bool force);
  void publishGPS(bool force);
  void publishInterval(bool force);
  void publishShutter(const std::string &state);

  std::string makeCamerasPayload(const std::vector<Control::target_status_t> &targets) const;
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
  void startInterval(void);
  void stopInterval(void);
  void intervalStep(void);
  void scheduleInterval(uint32_t delay_ms);

  static std::string jsonString(const void *object);
  static std::string trimTopic(const std::string &topic);
  static std::string typeName(Camera::Type type);

  mutable std::mutex m_Mutex;
  TaskHandle_t m_Task = nullptr;
  esp_mqtt_client_handle_t m_Client = nullptr;
  esp_timer_handle_t m_HoldTimer = nullptr;
  esp_timer_handle_t m_IntervalTimer = nullptr;

  std::atomic<bool> m_Connected = false;
  bool m_Reload = false;
  bool m_ManualDisconnect = false;
  bool m_StopClient = false;
  bool m_Blocked = false;
  bool m_ClearDiscovery = false;
  uint32_t m_ConnectFailures = 0;
  mutable uint32_t m_LastNetworkLogMs = 0;

  bool m_CameraListLoaded = false;
  bool m_IntervalRunning = false;
  interval_phase_t m_IntervalPhase = interval_phase_t::WAIT;
  uint32_t m_IntervalRemaining = 0;
  uint32_t m_IntervalTotal = 0;
  uint64_t m_IntervalNextMs = 0;

  bool m_HoldActive = false;
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
