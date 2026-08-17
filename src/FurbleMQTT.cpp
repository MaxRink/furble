#include "FurbleMQTT.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>

// M5PM1.h (pulled in transitively via FurblePlatform.h) leaks the Arduino GPIO
// macros LOW and HIGH, which collide with the Scan::Mode enumerators. furble is
// ESP-IDF and does not use them, so drop them before the NimBLE headers.
#undef LOW
#undef HIGH

#include "CameraList.h"
#include "Device.h"
#include "Scan.h"

#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {

constexpr const char *MQTT_LOG_TAG = "mqtt";
constexpr const char *HOME_ASSISTANT_STATUS = "homeassistant/status";

uint64_t nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

std::string trimWhitespace(const std::string &value) {
  size_t first = 0;
  size_t last = value.size();

  while ((first < last) && std::isspace(static_cast<unsigned char>(value[first]))) {
    first++;
  }
  while ((last > first) && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    last--;
  }

  return value.substr(first, last - first);
}

bool startsWith(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool parseUnsigned(const std::string &text, uint32_t &value) {
  const std::string trimmed = trimWhitespace(text);
  if (trimmed.empty()) {
    return false;
  }

  char *end = nullptr;
  const unsigned long parsed = std::strtoul(trimmed.c_str(), &end, 10);
  if ((end == trimmed.c_str()) || (*end != '\0') || (parsed > UINT32_MAX)) {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseNumber(const cJSON *object, const char *name, double &value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if ((item == nullptr) || !cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) {
    return false;
  }

  value = item->valuedouble;
  return true;
}

void addAvailability(cJSON *component, const std::string &topic) {
  cJSON_AddStringToObject(component, "availability_topic", topic.c_str());
  cJSON_AddStringToObject(component, "payload_available", "online");
  cJSON_AddStringToObject(component, "payload_not_available", "offline");
}

void addCameraAvailability(cJSON *component,
                           const std::string &hub_topic,
                           const std::string &camera_topic) {
  cJSON *availability = cJSON_AddArrayToObject(component, "availability");

  cJSON *hub = cJSON_CreateObject();
  cJSON_AddStringToObject(hub, "topic", hub_topic.c_str());
  cJSON_AddStringToObject(hub, "payload_available", "online");
  cJSON_AddStringToObject(hub, "payload_not_available", "offline");
  cJSON_AddItemToArray(availability, hub);

  cJSON *camera = cJSON_CreateObject();
  cJSON_AddStringToObject(camera, "topic", camera_topic.c_str());
  cJSON_AddStringToObject(camera, "value_template",
                          "{{ 'online' if value_json.connected else 'offline' }}");
  cJSON_AddStringToObject(camera, "payload_available", "online");
  cJSON_AddStringToObject(camera, "payload_not_available", "offline");
  cJSON_AddItemToArray(availability, camera);

  cJSON_AddStringToObject(component, "availability_mode", "all");
}

void addDevice(cJSON *root,
               const std::string &id,
               const std::string &name,
               const std::string &via_device) {
  cJSON *device = cJSON_AddObjectToObject(root, "dev");
  cJSON *identifiers = cJSON_AddArrayToObject(device, "identifiers");
  cJSON_AddItemToArray(identifiers, cJSON_CreateString(id.c_str()));
  cJSON_AddStringToObject(device, "name", name.c_str());
  cJSON_AddStringToObject(device, "manufacturer", "furble");
  cJSON_AddStringToObject(device, "model", "BLE camera shutter remote");
  cJSON_AddStringToObject(device, "sw_version", FURBLE_VERSION);
  if (!via_device.empty()) {
    cJSON_AddStringToObject(device, "via_device", via_device.c_str());
  }
}

void addOrigin(cJSON *root) {
  cJSON *origin = cJSON_AddObjectToObject(root, "o");
  cJSON_AddStringToObject(origin, "name", "furble");
  cJSON_AddStringToObject(origin, "sw_version", FURBLE_VERSION);
}

void addUniqueID(cJSON *component, const std::string &device_id, const char *entity) {
  cJSON_AddStringToObject(component, "unique_id", (device_id + "_" + entity).c_str());
}

uint32_t intervalMilliseconds(const SpinValue::nvs_t &value) {
  uint64_t multiplier = 0;
  switch (value.unit) {
    case SpinValue::UNIT_MS:
      multiplier = 1;
      break;
    case SpinValue::UNIT_SEC:
      multiplier = 1000;
      break;
    case SpinValue::UNIT_MIN:
      multiplier = 60 * 1000;
      break;
    case SpinValue::UNIT_NIL:
    case SpinValue::UNIT_INF:
      return 0;
  }

  const uint64_t milliseconds = static_cast<uint64_t>(value.value) * multiplier;
  return milliseconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(milliseconds);
}

uint32_t addMilliseconds(uint32_t first, uint32_t second) {
  const uint64_t total = static_cast<uint64_t>(first) + second;
  return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
}

int16_t rssiBucket(int rssi) {
  if ((rssi <= -127) || (rssi >= 0)) {
    return -127;
  }

  const int bucket = static_cast<int>(std::lround(static_cast<double>(rssi) / 5.0)) * 5;
  return static_cast<int16_t>(std::clamp(bucket, -125, -5));
}

}  // namespace

MQTT &MQTT::getInstance(void) {
  static MQTT instance;
  return instance;
}

void MQTT::init(void) {
  auto &instance = getInstance();
  if (Settings::load<Settings::MQTT>()) {
    CameraList::load();
    instance.m_CameraListLoaded = true;
  }
  instance.reloadSetting();
}

void MQTT::reloadSetting(void) {
  const bool enabled = Settings::load<Settings::MQTT>();

  // app_main owns the single esp_netif_init and esp_event_loop_create_default,
  // and the network module (WiFi now, Ethernet later) creates the interface.
  // MQTT only rides on top, so it never touches the shared network stack init.

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Reload = true;
    m_ManualDisconnect = false;
    m_Blocked = false;
    m_StopClient = false;
    m_ConnectFailures = 0;
  }

  if (!enabled && (m_Task == nullptr)) {
    return;
  }

  if (m_Task == nullptr) {
    const BaseType_t result = xTaskCreate(taskEntry, "mqtt", 6144, this, 2, &m_Task);
    if (result != pdPASS) {
      m_Task = nullptr;
      ESP_LOGE(MQTT_LOG_TAG, "Failed to create MQTT task.");
    }
  }
}

void MQTT::disconnect(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_ManualDisconnect = true;
  m_Reload = true;
  m_StopClient = true;
}

void MQTT::clearDiscovery(void) {
  bool connected = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_ClearDiscovery = true;
    connected = m_Connected.load();
  }

  if (!connected) {
    ESP_LOGW(MQTT_LOG_TAG, "MQTT is not connected, discovery clear is queued.");
  }
}

bool MQTT::isConfigured(void) const {
  return Settings::load<Settings::MQTT>() && !Settings::load<Settings::MQTT_URI>().empty();
}

void MQTT::taskEntry(void *param) {
  static_cast<MQTT *>(param)->task();
}

void MQTT::task(void) {
  while (true) {
    bool reload = false;
    bool manual_disconnect = false;
    bool blocked = false;
    bool stop_client = false;

    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      reload = m_Reload;
      m_Reload = false;
      manual_disconnect = m_ManualDisconnect;
      blocked = m_Blocked;
      stop_client = m_StopClient;
      m_StopClient = false;
    }

    if (reload) {
      stopClient();
      loadTopics();
      m_CameraListLoaded = false;
      m_LastCamerasPayload.clear();
      m_LastGPSPayload.clear();
      m_LastIntervalPayload.clear();
      m_LastCamerasSampleMs = 0;
      m_LastBatterySampleMs = 0;
      m_LastBatteryMs = 0;
      m_LastGPSMs = 0;
    }

    const bool enabled = Settings::load<Settings::MQTT>();
    if (!enabled || manual_disconnect || blocked || !isConfigured()) {
      if (m_Client != nullptr) {
        stopClient();
      }
      vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      continue;
    }

    if (stop_client && (m_Client != nullptr)) {
      stopClient();
      vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      continue;
    }

    if ((m_Client == nullptr) && networkReady()) {
      if (!startClient()) {
        m_ConnectFailures++;
        ESP_LOGW(MQTT_LOG_TAG, "MQTT client start failed, attempt %lu.",
                 static_cast<unsigned long>(m_ConnectFailures));
        if (m_ConnectFailures >= MAX_CONNECT_FAILURES) {
          const std::lock_guard<std::mutex> lock(m_Mutex);
          m_Blocked = true;
          ESP_LOGE(MQTT_LOG_TAG, "MQTT retry limit reached. Use mqtt connect to retry.");
        }
      }
    }

    if (m_Connected.load()) {
      if (m_ClearDiscovery) {
        clearDiscoveryRecords();
        const std::lock_guard<std::mutex> lock(m_Mutex);
        m_ClearDiscovery = false;
      } else {
        publishCameras(false);
        publishBattery(false);
        publishGPS(false);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
  }
}

bool MQTT::networkReady(void) const {
  // Key on the generic network-up seam: any interface that is up and holds an
  // assigned IPv4 address. This tracks the IP_EVENT_*_GOT_IP outcome without
  // binding to WiFi, so a wired Ethernet interface satisfies it just as well.
  for (esp_netif_t *netif = esp_netif_next_unsafe(nullptr); netif != nullptr;
       netif = esp_netif_next_unsafe(netif)) {
    if (!esp_netif_is_netif_up(netif)) {
      continue;
    }
    esp_netif_ip_info_t info = {};
    if ((esp_netif_get_ip_info(netif, &info) == ESP_OK) && (info.ip.addr != 0)) {
      return true;
    }
  }

  const uint32_t now = static_cast<uint32_t>(nowMs());
  if ((now - m_LastNetworkLogMs) > 10000) {
    m_LastNetworkLogMs = now;
    ESP_LOGI(MQTT_LOG_TAG, "Waiting for a network interface with an assigned IP.");
  }
  return false;
}

void MQTT::loadTopics(void) {
  m_Base = trimTopic(Settings::load<Settings::MQTT_BASE>());
  if (m_Base.empty()) {
    m_Base = "furble";
  }

  m_ID = Device::getStringID();
  m_URI = trimWhitespace(Settings::load<Settings::MQTT_URI>());
  m_User = Settings::load<Settings::MQTT_USER>();
  m_Password = Settings::load<Settings::MQTT_PASS>();
  m_RootTopic = m_Base + "/" + m_ID;
  m_StatusTopic = m_RootTopic + "/status";
  m_CamerasTopic = m_RootTopic + "/state/cameras";
  m_BatteryTopic = m_RootTopic + "/state/battery";
  m_GPSTopic = m_RootTopic + "/state/gps";
  m_IntervalTopic = m_RootTopic + "/state/interval";
  m_ShutterTopic = m_RootTopic + "/state/shutter";
  m_ErrorTopic = m_RootTopic + "/state/error";
  m_ShutterCommandTopic = m_RootTopic + "/cmd/shutter";
  m_FocusCommandTopic = m_RootTopic + "/cmd/focus";
  m_IntervalCommandTopic = m_RootTopic + "/cmd/interval";
  m_ConnectCommandTopic = m_RootTopic + "/cmd/connect";
  m_DisconnectCommandTopic = m_RootTopic + "/cmd/disconnect";
  m_LocationCommandTopic = m_RootTopic + "/cmd/location";
  m_HomeAssistantTopic = "homeassistant/device/furble_" + m_ID + "/config";
}

bool MQTT::startClient(void) {
  if (m_Client != nullptr) {
    return true;
  }

  loadTopics();
  if (m_URI.empty()) {
    return false;
  }

  esp_mqtt_client_config_t config = {};
  config.broker.address.uri = m_URI.c_str();
  config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  config.credentials.client_id = m_ID.c_str();
  config.credentials.username = m_User.empty() ? nullptr : m_User.c_str();
  config.credentials.authentication.password = m_Password.empty() ? nullptr : m_Password.c_str();
  config.session.last_will.topic = m_StatusTopic.c_str();
  config.session.last_will.msg = "offline";
  config.session.last_will.msg_len = 7;
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 1;
  config.session.keepalive = 60;
  config.session.disable_clean_session = false;
  config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  config.network.reconnect_timeout_ms = 10000;
  config.network.timeout_ms = 10000;
  config.buffer.size = 2048;
  config.buffer.out_size = 2048;

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
  if (client == nullptr) {
    return false;
  }

  esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, eventHandler, this);
  if (err == ESP_OK) {
    m_Client = client;
    if (m_HoldTimer == nullptr) {
      const esp_timer_create_args_t args = {
          .callback = holdTimerCallback,
          .arg = this,
          .dispatch_method = ESP_TIMER_TASK,
          .name = "mqtt_hold",
          .skip_unhandled_events = false,
      };
      esp_timer_create(&args, &m_HoldTimer);
    }
    if (m_IntervalTimer == nullptr) {
      const esp_timer_create_args_t args = {
          .callback = intervalTimerCallback,
          .arg = this,
          .dispatch_method = ESP_TIMER_TASK,
          .name = "mqtt_interval",
          .skip_unhandled_events = false,
      };
      esp_timer_create(&args, &m_IntervalTimer);
    }
    err = esp_mqtt_client_start(client);
  }
  if (err != ESP_OK) {
    m_Client = nullptr;
    esp_mqtt_client_destroy(client);
    ESP_LOGW(MQTT_LOG_TAG, "MQTT start failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

void MQTT::stopClient(void) {
  releaseHold();
  stopInterval();

  esp_mqtt_client_handle_t client = m_Client;
  m_Client = nullptr;
  m_Connected.store(false);
  if (client == nullptr) {
    return;
  }

  esp_mqtt_client_stop(client);
  esp_mqtt_client_destroy(client);
}

void MQTT::eventHandler(void *handler_arg,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void *event_data) {
  (void)event_base;
  (void)event_id;
  auto *mqtt = static_cast<MQTT *>(handler_arg);
  mqtt->handleEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
}

void MQTT::handleEvent(esp_mqtt_event_handle_t event) {
  if (event == nullptr) {
    return;
  }

  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
      m_Connected.store(true);
      m_ConnectFailures = 0;
      ESP_LOGI(MQTT_LOG_TAG, "MQTT connected.");
      publish(m_StatusTopic, "online", 1, true);
      publishState(true);
      if (Settings::load<Settings::MQTT_HA>()) {
        publishDiscovery();
      }
      esp_mqtt_client_subscribe(event->client, (m_RootTopic + "/cmd/#").c_str(), 1);
      if (Settings::load<Settings::MQTT_HA>()) {
        esp_mqtt_client_subscribe(event->client, HOME_ASSISTANT_STATUS, 0);
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      m_Connected.store(false);
      if (!m_ManualDisconnect && !m_StopClient) {
        m_ConnectFailures++;
        ESP_LOGW(MQTT_LOG_TAG, "MQTT disconnected, attempt %lu.",
                 static_cast<unsigned long>(m_ConnectFailures));
        if (m_ConnectFailures >= MAX_CONNECT_FAILURES) {
          const std::lock_guard<std::mutex> lock(m_Mutex);
          m_Blocked = true;
          m_StopClient = true;
        }
      }
      break;

    case MQTT_EVENT_DATA:
      handleData(event);
      break;

    case MQTT_EVENT_ERROR:
      if (event->error_handle != nullptr) {
        ESP_LOGW(MQTT_LOG_TAG, "MQTT error type %d.",
                 static_cast<int>(event->error_handle->error_type));
      }
      break;

    default:
      break;
  }
}

void MQTT::handleData(const esp_mqtt_event_handle_t event) {
  if ((event->topic == nullptr) || (event->data == nullptr) || (event->topic_len <= 0)) {
    return;
  }

  if ((event->current_data_offset != 0) || (event->data_len != event->total_data_len)) {
    ESP_LOGW(MQTT_LOG_TAG, "Ignoring fragmented MQTT command.");
    return;
  }

  const std::string topic(event->topic, static_cast<size_t>(event->topic_len));
  const std::string payload(event->data, static_cast<size_t>(event->data_len));
  handleCommand(topic, payload);
}

void MQTT::handleCommand(const std::string &topic, const std::string &payload) {
  const std::string value = trimWhitespace(payload);

  if ((topic == HOME_ASSISTANT_STATUS) && (value == "online")) {
    if (Settings::load<Settings::MQTT_HA>()) {
      publishDiscovery();
    }
    return;
  }

  if (topic == m_ShutterCommandTopic) {
    if (value == "press") {
      if (!sendCommand(Control::CMD_SHUTTER_PRESS)) {
        publishError("shutter press rejected");
      } else {
        publishShutter("held");
      }
    } else if (value == "release") {
      releaseHold();
      if (!sendCommand(Control::CMD_SHUTTER_RELEASE)) {
        publishError("shutter release rejected");
      } else {
        publishShutter("idle");
      }
    } else if (startsWith(value, "hold ")) {
      uint32_t duration = 0;
      if (!parseUnsigned(value.substr(5), duration) || (duration > MAX_HOLD_MS)) {
        publishError("shutter hold must be 0-60000 ms");
      } else if (!sendHold(duration)) {
        publishError("shutter hold rejected");
      }
    } else {
      publishError("unknown shutter command");
    }
    return;
  }

  if (topic == m_FocusCommandTopic) {
    Control::cmd_t command;
    if (value == "press") {
      command = Control::CMD_FOCUS_PRESS;
    } else if (value == "release") {
      command = Control::CMD_FOCUS_RELEASE;
    } else {
      publishError("unknown focus command");
      return;
    }

    if (!sendCommand(command)) {
      publishError("focus command rejected");
    }
    return;
  }

  if (topic == m_IntervalCommandTopic) {
    if (value == "start") {
      startInterval();
    } else if (value == "stop") {
      stopInterval();
    } else {
      publishError("unknown interval command");
    }
    return;
  }

  if (topic == m_ConnectCommandTopic) {
    CameraList::load();
    m_CameraListLoaded = true;

    if ((Control::getInstance().getState() != Control::STATE_IDLE)
        && (Control::getInstance().getState() != Control::STATE_CONNECT_FAILED)) {
      publishError("connect rejected while cameras are active");
      return;
    }

    bool all = value == "all";
    uint32_t index = 0;
    const bool numeric = parseUnsigned(value, index);
    if (!all && !numeric && value.empty()) {
      all = true;
    }

    bool found = all;
    for (size_t n = 0; n < CameraList::size(); n++) {
      const auto camera = CameraList::get(n);
      bool active = all;
      if (!all && numeric) {
        active = n == index;
      } else if (!all && !numeric) {
        active = Control::getCameraID(*camera) == value;
      }
      camera->setActive(active);
      found = found || active;
    }

    if (!found) {
      publishError("camera not found");
      return;
    }

    auto &control = Control::getInstance();
    for (size_t n = 0; n < CameraList::size(); n++) {
      if (CameraList::get(n)->isActive()) {
        control.addActive(CameraList::get(n));
      }
    }
    control.connectAll(Settings::load<Settings::RECONNECT>());
    publishCameras(true);
    return;
  }

  if (topic == m_DisconnectCommandTopic) {
    Scan::getInstance().stop();
    Control::getInstance().disconnect();
    publishCameras(true);
    return;
  }

  if (topic == m_LocationCommandTopic) {
    handleLocation(payload);
    return;
  }
}

void MQTT::handleLocation(const std::string &payload) {
  cJSON *root = cJSON_ParseWithLength(payload.c_str(), payload.size());
  if (root == nullptr) {
    publishError("location payload is not JSON");
    return;
  }

  double latitude = 0;
  double longitude = 0;
  double timestamp = 0;
  if (!parseNumber(root, "latitude", latitude) || !parseNumber(root, "longitude", longitude)
      || !parseNumber(root, "timestamp", timestamp) || (timestamp <= 0)) {
    cJSON_Delete(root);
    publishError("location needs latitude, longitude and timestamp");
    return;
  }

  if ((latitude < -90.0) || (latitude > 90.0) || (longitude < -180.0) || (longitude > 180.0)) {
    cJSON_Delete(root);
    publishError("location coordinates are out of range");
    return;
  }

  if (timestamp > 20000000000.0) {
    timestamp /= 1000.0;
  }
  if ((timestamp < 1.0) || (timestamp > static_cast<double>(std::numeric_limits<time_t>::max()))) {
    cJSON_Delete(root);
    publishError("location timestamp is invalid");
    return;
  }

  const time_t seconds = static_cast<time_t>(timestamp);
  struct tm utc = {};
  if (gmtime_r(&seconds, &utc) == nullptr) {
    cJSON_Delete(root);
    publishError("location timestamp is invalid");
    return;
  }

  GPS::external_fix_t fix = {};
  fix.gps.latitude = latitude;
  fix.gps.longitude = longitude;
  fix.position_valid = true;
  fix.timesync.year = static_cast<unsigned int>(utc.tm_year + 1900);
  fix.timesync.month = static_cast<unsigned int>(utc.tm_mon + 1);
  fix.timesync.day = static_cast<unsigned int>(utc.tm_mday);
  fix.timesync.hour = static_cast<unsigned int>(utc.tm_hour);
  fix.timesync.minute = static_cast<unsigned int>(utc.tm_min);
  fix.timesync.second = static_cast<unsigned int>(utc.tm_sec);
  fix.timesync.centisecond = 0;
  fix.time_valid = true;

  double number = 0;
  if (parseNumber(root, "altitude", number)) {
    fix.gps.altitude = number;
    fix.altitude_valid = true;
  }
  if (parseNumber(root, "accuracy", number)) {
    if ((number < 0) || (number > static_cast<double>(FLT_MAX))) {
      cJSON_Delete(root);
      publishError("location accuracy is invalid");
      return;
    }
    fix.accuracy_m = static_cast<float>(number);
    fix.accuracy_valid = true;
  }
  if (parseNumber(root, "satellites", number)) {
    fix.gps.satellites = static_cast<unsigned int>(std::clamp(number, 0.0, 255.0));
  }

  cJSON_Delete(root);
  if (!GPS::getInstance().setExternalFix(fix)) {
    publishError("location fix rejected");
    return;
  }

  m_HaveExternalLocation = true;
  ESP_LOGI(MQTT_LOG_TAG, "Accepted external GPS fix.");
}

bool MQTT::sendCommand(Control::cmd_t command) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    return false;
  }

  return control.sendCommand(command) == pdTRUE;
}

bool MQTT::sendHold(uint32_t duration_ms) {
  releaseHold();
  if (!sendCommand(Control::CMD_SHUTTER_PRESS)) {
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_HoldActive = true;
  }

  if (m_HoldTimer == nullptr) {
    releaseHold();
    return false;
  }

  esp_timer_stop(m_HoldTimer);
  const esp_err_t err =
      esp_timer_start_once(m_HoldTimer, std::max<uint32_t>(duration_ms, 1) * 1000ULL);
  if (err != ESP_OK) {
    releaseHold();
    return false;
  }

  publishShutter("held");
  return true;
}

void MQTT::holdTimerCallback(void *arg) {
  static_cast<MQTT *>(arg)->releaseHold();
}

void MQTT::releaseHold(void) {
  bool active = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    active = m_HoldActive;
    m_HoldActive = false;
  }

  if (m_HoldTimer != nullptr) {
    esp_timer_stop(m_HoldTimer);
  }
  if (active) {
    sendCommand(Control::CMD_SHUTTER_RELEASE);
    publishShutter("idle");
  }
}

void MQTT::startInterval(void) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_ACTIVE) {
    publishError("interval start rejected: no camera connected");
    return;
  }

  interval_t settings = Settings::load<Settings::INTERVAL>();
  const uint32_t total =
      settings.count.unit == SpinValue::UNIT_INF ? UINT32_MAX : settings.count.value;
  if (total == 0) {
    publishError("interval count is zero");
    return;
  }
  if (m_IntervalTimer == nullptr) {
    publishError("interval timer unavailable");
    return;
  }

  stopInterval();
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_IntervalRunning = true;
    m_IntervalPhase = interval_phase_t::WAIT;
    m_IntervalRemaining = total;
    m_IntervalTotal = total;
  }

  scheduleInterval(intervalMilliseconds(settings.wait));
  publishInterval(true);
}

void MQTT::stopInterval(void) {
  bool was_running = false;
  bool shutter_open = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    was_running = m_IntervalRunning;
    shutter_open = m_IntervalPhase == interval_phase_t::SHUTTER;
    m_IntervalRunning = false;
    m_IntervalRemaining = 0;
    m_IntervalTotal = 0;
    m_IntervalNextMs = 0;
  }

  if (m_IntervalTimer != nullptr) {
    esp_timer_stop(m_IntervalTimer);
  }
  if (shutter_open) {
    sendCommand(Control::CMD_SHUTTER_RELEASE);
  }
  if (was_running) {
    publishInterval(true);
  }
}

void MQTT::scheduleInterval(uint32_t delay_ms) {
  if (m_IntervalTimer == nullptr) {
    return;
  }

  const uint32_t delay = std::max<uint32_t>(delay_ms, 1);
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_IntervalNextMs = nowMs() + delay;
  }
  esp_timer_stop(m_IntervalTimer);
  esp_timer_start_once(m_IntervalTimer, static_cast<uint64_t>(delay) * 1000ULL);
}

void MQTT::intervalTimerCallback(void *arg) {
  static_cast<MQTT *>(arg)->intervalStep();
}

void MQTT::intervalStep(void) {
  interval_phase_t phase;
  uint32_t remaining;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_IntervalRunning) {
      return;
    }
    phase = m_IntervalPhase;
    remaining = m_IntervalRemaining;
  }

  interval_t settings = Settings::load<Settings::INTERVAL>();

  if (phase == interval_phase_t::WAIT) {
    if (!sendCommand(Control::CMD_SHUTTER_PRESS)) {
      publishError("interval shutter press rejected");
      stopInterval();
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_IntervalPhase = interval_phase_t::SHUTTER;
    }
    scheduleInterval(intervalMilliseconds(settings.shutter));
  } else {
    if (!sendCommand(Control::CMD_SHUTTER_RELEASE)) {
      publishError("interval shutter release rejected");
      stopInterval();
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_IntervalPhase = interval_phase_t::WAIT;
    }
    if ((remaining != UINT32_MAX) && (remaining <= 1)) {
      stopInterval();
    } else {
      {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_IntervalRemaining != UINT32_MAX) {
          m_IntervalRemaining--;
        }
        m_IntervalPhase = interval_phase_t::WAIT;
      }
      scheduleInterval(addMilliseconds(intervalMilliseconds(settings.delay),
                                       intervalMilliseconds(settings.wait)));
    }
  }
  publishInterval(true);
}

bool MQTT::publish(const std::string &topic, const std::string &payload, int qos, bool retain) {
  if (!m_Connected.load()) {
    return false;
  }

  esp_mqtt_client_handle_t client = m_Client;
  if (client == nullptr) {
    return false;
  }

  const int message_id = esp_mqtt_client_enqueue(
      client, topic.c_str(), payload.c_str(), static_cast<int>(payload.size()), qos, retain, true);
  if (message_id < 0) {
    ESP_LOGW(MQTT_LOG_TAG, "MQTT enqueue failed for %s.", topic.c_str());
    return false;
  }
  return true;
}

void MQTT::publishError(const std::string &message) {
  publish(m_ErrorTopic, message, 1, false);
  ESP_LOGW(MQTT_LOG_TAG, "%s", message.c_str());
}

void MQTT::publishState(bool force) {
  (void)force;
  publishCameras(true);
  publishBattery(true);
  publishGPS(true);
  publishInterval(true);
}

void MQTT::publishCameras(bool force) {
  const uint64_t now = nowMs();
  if (!force && ((now - m_LastCamerasSampleMs) < CAMERAS_PERIOD_MS)) {
    return;
  }
  m_LastCamerasSampleMs = now;

  if (!m_CameraListLoaded && (Control::getInstance().getState() == Control::STATE_IDLE)) {
    CameraList::load();
    m_CameraListLoaded = true;
  }

  const auto targets = Control::getInstance().getTargetStatus();
  const std::string payload = makeCamerasPayload(targets);
  const bool changed = force || (payload != m_LastCamerasPayload);
  if (changed) {
    if (publish(m_CamerasTopic, payload, 1, true)) {
      m_LastCamerasPayload = payload;
    }
  }

  if (!changed) {
    return;
  }

  for (size_t n = 0; n < CameraList::size(); n++) {
    const auto camera = CameraList::get(n);
    Control::target_status_t status = {Control::getCameraID(*camera),
                                       camera->getName(),
                                       camera->getType(),
                                       false,
                                       camera->getConnectProgress(),
                                       static_cast<int16_t>(camera->getRSSI())};
    for (const auto &target : targets) {
      if (target.id == status.id) {
        status = target;
        break;
      }
    }
    publish(m_RootTopic + "/camera/" + status.id + "/state",
            makeCameraStatePayload(status, camera.get()), 1, true);
  }
}

void MQTT::publishBattery(bool force) {
  const uint64_t now = nowMs();
  if (!force && ((now - m_LastBatterySampleMs) < 1000)) {
    return;
  }

  const Platform::battery_t battery = Platform::getInstance().readBattery();
  const std::string payload = makeBatteryPayload(battery);
  const bool charging_changed = !m_HaveBatterySample || (battery.charging != m_LastCharging);
  const bool due = (now - m_LastBatteryMs) >= BATTERY_PERIOD_MS;
  if (force || charging_changed || due) {
    if (publish(m_BatteryTopic, payload, 0, true)) {
      m_LastBatteryMs = now;
    }
  }
  m_LastBatterySampleMs = now;
  m_LastCharging = battery.charging;
  m_HaveBatterySample = true;
}

void MQTT::publishGPS(bool force) {
  if (!Settings::load<Settings::GPS>() && !m_HaveExternalLocation) {
    return;
  }

  GPS::external_fix_t fix = {};
  if (!GPS::getInstance().getCurrentFix(fix)) {
    return;
  }

  const uint64_t now = nowMs();
  if (!force && ((now - m_LastGPSMs) < GPS_PERIOD_MS)) {
    return;
  }
  const std::string payload = makeGPSPayload();
  if (payload.empty()) {
    return;
  }
  if (force || (payload != m_LastGPSPayload)) {
    if (publish(m_GPSTopic, payload, 0, false)) {
      m_LastGPSPayload = payload;
      m_LastGPSMs = now;
    }
  } else {
    m_LastGPSMs = now;
  }
}

void MQTT::publishInterval(bool force) {
  const std::string payload = makeIntervalPayload();
  if (force || (payload != m_LastIntervalPayload)) {
    if (publish(m_IntervalTopic, payload, 1, false)) {
      m_LastIntervalPayload = payload;
    }
  }
}

void MQTT::publishShutter(const std::string &state) {
  publish(m_ShutterTopic, state, 1, true);
}

std::string MQTT::makeCamerasPayload(const std::vector<Control::target_status_t> &targets) const {
  cJSON *root = cJSON_CreateArray();
  if (root == nullptr) {
    return "[]";
  }

  for (size_t n = 0; n < CameraList::size(); n++) {
    const auto camera = CameraList::get(n);
    const std::string id = Control::getCameraID(*camera);
    bool connected = false;
    uint8_t progress = camera->getConnectProgress();
    int16_t rssi = -127;
    for (const auto &target : targets) {
      if (target.id == id) {
        connected = target.connected;
        progress = target.progress;
        rssi = target.rssi;
        break;
      }
    }

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", id.c_str());
    cJSON_AddStringToObject(item, "name", camera->getName().c_str());
    cJSON_AddStringToObject(item, "type", typeName(camera->getType()).c_str());
    cJSON_AddBoolToObject(item, "connected", connected);
    cJSON_AddNumberToObject(item, "progress", progress);
    cJSON_AddNumberToObject(item, "rssi", rssiBucket(rssi));
    cJSON_AddItemToArray(root, item);
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string MQTT::makeCameraStatePayload(const Control::target_status_t &status,
                                         const Camera *camera) const {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    return "{}";
  }

  cJSON_AddStringToObject(root, "id", status.id.c_str());
  cJSON_AddStringToObject(root, "name", status.name.c_str());
  cJSON_AddStringToObject(root, "type", typeName(status.type).c_str());
  cJSON_AddBoolToObject(root, "connected", status.connected);
  cJSON_AddNumberToObject(root, "progress", status.progress);
  cJSON_AddStringToObject(
      root, "state",
      status.connected ? "connected" : (status.progress > 0 ? "connecting" : "disconnected"));
  cJSON_AddNumberToObject(root, "rssi", rssiBucket(status.rssi));
  if (camera != nullptr) {
    cJSON_AddBoolToObject(root, "active", camera->isActive());
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string MQTT::makeBatteryPayload(const Platform::battery_t &battery) const {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    return "{}";
  }

  cJSON_AddNumberToObject(root, "level", battery.level);
  cJSON_AddNumberToObject(root, "voltage", battery.voltage);
  cJSON_AddNumberToObject(root, "current", battery.current);
  cJSON_AddBoolToObject(root, "charging", battery.charging);

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string MQTT::makeGPSPayload(void) const {
  GPS::external_fix_t fix = {};
  if (!GPS::getInstance().getCurrentFix(fix)) {
    return {};
  }

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    return "{}";
  }

  const GPS::source_t source = GPS::getInstance().getSource();
  cJSON_AddBoolToObject(root, "fix", fix.position_valid);
  cJSON_AddStringToObject(root, "source", source == GPS::SOURCE_COMPANION ? "external" : "uart");
  cJSON_AddNumberToObject(root, "satellites", fix.gps.satellites);
  cJSON_AddNumberToObject(root, "latitude", fix.gps.latitude);
  cJSON_AddNumberToObject(root, "longitude", fix.gps.longitude);
  cJSON_AddNumberToObject(root, "age", fix.age_ms);
  if (fix.altitude_valid) {
    cJSON_AddNumberToObject(root, "altitude", fix.gps.altitude);
  }
  if (fix.accuracy_valid) {
    cJSON_AddNumberToObject(root, "accuracy", fix.accuracy_m);
  }

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string MQTT::makeIntervalPayload(void) const {
  bool running;
  uint32_t remaining;
  uint32_t total;
  uint64_t next;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    running = m_IntervalRunning;
    remaining = m_IntervalRemaining;
    total = m_IntervalTotal;
    next = m_IntervalNextMs;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    return "{}";
  }

  const uint64_t now = nowMs();
  const uint64_t next_ms = (next > now) ? (next - now) : 0;
  cJSON_AddBoolToObject(root, "running", running);
  cJSON_AddNumberToObject(root, "remaining", remaining);
  cJSON_AddNumberToObject(root, "total", total);
  cJSON_AddNumberToObject(root, "next_ms", next_ms);

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  return payload;
}

std::string MQTT::jsonString(const void *object) {
  const auto *json = static_cast<const cJSON *>(object);
  char *text = cJSON_PrintUnformatted(json);
  if (text == nullptr) {
    return {};
  }

  std::string result(text);
  cJSON_free(text);
  return result;
}

std::string MQTT::trimTopic(const std::string &topic) {
  std::string value = trimWhitespace(topic);
  while (!value.empty() && (value.front() == '/')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == '/')) {
    value.pop_back();
  }
  return value;
}

std::string MQTT::typeName(Camera::Type type) {
  switch (type) {
    case Camera::Type::FUJIFILM_BASIC:
      return "fujifilm_basic";
    case Camera::Type::CANON_EOS_SMART:
      return "canon_eos_smart";
    case Camera::Type::CANON_EOS_REMOTE:
      return "canon_eos_remote";
    case Camera::Type::MOBILE_DEVICE:
      return "mobile_device";
    case Camera::Type::FAUXNY:
      return "fauxny";
    case Camera::Type::NIKON:
      return "nikon";
    case Camera::Type::SONY:
      return "sony";
    case Camera::Type::FUJIFILM_SECURE:
      return "fujifilm_secure";
    case Camera::Type::RICOH:
      return "ricoh";
    case Camera::Type::PANASONIC_LUMIX:
      return "panasonic_lumix";
    case Camera::Type::DJI_OSMO:
      return "dji_osmo";
  }
  return "unknown";
}

void MQTT::publishDiscoveryDevice(const std::string &topic,
                                  const std::string &device_id,
                                  const std::string &name,
                                  const std::string &via_device,
                                  const std::string &components) {
  cJSON *root = cJSON_CreateObject();
  cJSON *component_object = cJSON_ParseWithLength(components.c_str(), components.size());
  if ((root == nullptr) || (component_object == nullptr)) {
    cJSON_Delete(root);
    cJSON_Delete(component_object);
    return;
  }

  addDevice(root, device_id, name, via_device);
  addOrigin(root);
  cJSON_AddItemToObject(root, "cmps", component_object);

  const std::string payload = jsonString(root);
  cJSON_Delete(root);
  publish(topic, payload, 1, true);
}

void MQTT::publishDiscovery(void) {
  if (!Settings::load<Settings::MQTT_HA>()) {
    return;
  }

  if (!m_CameraListLoaded && (Control::getInstance().getState() == Control::STATE_IDLE)) {
    CameraList::load();
    m_CameraListLoaded = true;
  }

  const std::string hub_id = "furble_" + m_ID;
  cJSON *hub_components = cJSON_CreateObject();
  if (hub_components == nullptr) {
    return;
  }

  auto addHubButton = [&](const char *key, const char *name, const std::string &command_topic,
                          const char *payload_press) {
    cJSON *component = cJSON_AddObjectToObject(hub_components, key);
    cJSON_AddStringToObject(component, "p", "button");
    cJSON_AddStringToObject(component, "name", name);
    addUniqueID(component, hub_id, key);
    cJSON_AddStringToObject(component, "command_topic", command_topic.c_str());
    cJSON_AddStringToObject(component, "payload_press", payload_press);
    addAvailability(component, m_StatusTopic);
  };

  addHubButton("shutter", "Shutter", m_ShutterCommandTopic, "hold 200");
  addHubButton("focus", "Focus", m_FocusCommandTopic, "press");

  cJSON *interval = cJSON_AddObjectToObject(hub_components, "interval");
  cJSON_AddStringToObject(interval, "p", "switch");
  cJSON_AddStringToObject(interval, "name", "Intervalometer");
  addUniqueID(interval, hub_id, "interval");
  cJSON_AddStringToObject(interval, "command_topic", m_IntervalCommandTopic.c_str());
  cJSON_AddStringToObject(interval, "state_topic", m_IntervalTopic.c_str());
  cJSON_AddStringToObject(interval, "payload_on", "start");
  cJSON_AddStringToObject(interval, "payload_off", "stop");
  cJSON_AddStringToObject(interval, "state_on", "true");
  cJSON_AddStringToObject(interval, "state_off", "false");
  cJSON_AddStringToObject(interval, "value_template", "{{ value_json.running }}");
  addAvailability(interval, m_StatusTopic);

  cJSON *battery = cJSON_AddObjectToObject(hub_components, "battery");
  cJSON_AddStringToObject(battery, "p", "sensor");
  cJSON_AddStringToObject(battery, "name", "Battery");
  addUniqueID(battery, hub_id, "battery");
  cJSON_AddStringToObject(battery, "state_topic", m_BatteryTopic.c_str());
  cJSON_AddStringToObject(battery, "value_template", "{{ value_json.level }}");
  cJSON_AddStringToObject(battery, "device_class", "battery");
  cJSON_AddStringToObject(battery, "unit_of_measurement", "%");
  cJSON_AddStringToObject(battery, "state_class", "measurement");
  addAvailability(battery, m_StatusTopic);

  cJSON *camera_count = cJSON_AddObjectToObject(hub_components, "cameras");
  cJSON_AddStringToObject(camera_count, "p", "sensor");
  cJSON_AddStringToObject(camera_count, "name", "Connected cameras");
  addUniqueID(camera_count, hub_id, "cameras");
  cJSON_AddStringToObject(camera_count, "state_topic", m_CamerasTopic.c_str());
  cJSON_AddStringToObject(
      camera_count, "value_template",
      "{{ value_json | selectattr('connected', 'equalto', true) | list | count }}");
  cJSON_AddStringToObject(camera_count, "state_class", "measurement");
  addAvailability(camera_count, m_StatusTopic);

  cJSON *camera_connected = cJSON_AddObjectToObject(hub_components, "camera_connected");
  cJSON_AddStringToObject(camera_connected, "p", "binary_sensor");
  cJSON_AddStringToObject(camera_connected, "name", "Camera connected");
  addUniqueID(camera_connected, hub_id, "camera_connected");
  cJSON_AddStringToObject(camera_connected, "state_topic", m_CamerasTopic.c_str());
  cJSON_AddStringToObject(camera_connected, "value_template",
                          "{{ 'ON' if (value_json | selectattr('connected', 'equalto', true) | "
                          "list | count) > 0 else 'OFF' }}");
  cJSON_AddStringToObject(camera_connected, "payload_on", "ON");
  cJSON_AddStringToObject(camera_connected, "payload_off", "OFF");
  cJSON_AddStringToObject(camera_connected, "device_class", "connectivity");
  addAvailability(camera_connected, m_StatusTopic);

  cJSON *voltage = cJSON_AddObjectToObject(hub_components, "voltage");
  cJSON_AddStringToObject(voltage, "p", "sensor");
  cJSON_AddStringToObject(voltage, "name", "Battery voltage");
  addUniqueID(voltage, hub_id, "voltage");
  cJSON_AddStringToObject(voltage, "state_topic", m_BatteryTopic.c_str());
  cJSON_AddStringToObject(voltage, "value_template", "{{ value_json.voltage }}");
  cJSON_AddStringToObject(voltage, "unit_of_measurement", "mV");
  cJSON_AddStringToObject(voltage, "state_class", "measurement");
  cJSON_AddStringToObject(voltage, "entity_category", "diagnostic");
  addAvailability(voltage, m_StatusTopic);

  cJSON *gps = cJSON_AddObjectToObject(hub_components, "gps");
  cJSON_AddStringToObject(gps, "p", "device_tracker");
  cJSON_AddStringToObject(gps, "name", "GPS");
  addUniqueID(gps, hub_id, "gps");
  cJSON_AddStringToObject(gps, "state_topic", m_GPSTopic.c_str());
  cJSON_AddStringToObject(gps, "value_template",
                          "{{ value_json.latitude }},{{ value_json.longitude }}");
  cJSON_AddStringToObject(gps, "json_attributes_topic", m_GPSTopic.c_str());
  cJSON_AddStringToObject(gps, "source_type", "gps");
  addAvailability(gps, m_StatusTopic);

  const std::string hub_payload = jsonString(hub_components);
  cJSON_Delete(hub_components);
  publishDiscoveryDevice(m_HomeAssistantTopic, hub_id, "furble " + m_ID, {}, hub_payload);

  std::vector<std::string> current_ids;
  for (size_t n = 0; n < CameraList::size(); n++) {
    const auto camera = CameraList::get(n);
    const std::string camera_id = Control::getCameraID(*camera);
    const std::string device_id = hub_id + "_" + camera_id;
    cJSON *components = cJSON_CreateObject();
    if (components == nullptr) {
      continue;
    }

    const std::string camera_state_topic = m_RootTopic + "/camera/" + camera_id + "/state";
    cJSON *connect = cJSON_AddObjectToObject(components, "connect");
    cJSON_AddStringToObject(connect, "p", "button");
    cJSON_AddStringToObject(connect, "name", "Connect");
    addUniqueID(connect, device_id, "connect");
    cJSON_AddStringToObject(connect, "command_topic", m_ConnectCommandTopic.c_str());
    cJSON_AddStringToObject(connect, "payload_press", camera_id.c_str());
    addCameraAvailability(connect, m_StatusTopic, camera_state_topic);

    cJSON *connected = cJSON_AddObjectToObject(components, "connected");
    cJSON_AddStringToObject(connected, "p", "binary_sensor");
    cJSON_AddStringToObject(connected, "name", "Connected");
    addUniqueID(connected, device_id, "connected");
    cJSON_AddStringToObject(connected, "state_topic", camera_state_topic.c_str());
    cJSON_AddStringToObject(connected, "value_template",
                            "{{ 'ON' if value_json.connected else 'OFF' }}");
    cJSON_AddStringToObject(connected, "payload_on", "ON");
    cJSON_AddStringToObject(connected, "payload_off", "OFF");
    cJSON_AddStringToObject(connected, "device_class", "connectivity");
    addCameraAvailability(connected, m_StatusTopic, camera_state_topic);

    cJSON *rssi = cJSON_AddObjectToObject(components, "rssi");
    cJSON_AddStringToObject(rssi, "p", "sensor");
    cJSON_AddStringToObject(rssi, "name", "Link RSSI");
    addUniqueID(rssi, device_id, "rssi");
    cJSON_AddStringToObject(rssi, "state_topic", camera_state_topic.c_str());
    cJSON_AddStringToObject(rssi, "value_template", "{{ value_json.rssi }}");
    cJSON_AddStringToObject(rssi, "device_class", "signal_strength");
    cJSON_AddStringToObject(rssi, "unit_of_measurement", "dBm");
    cJSON_AddStringToObject(rssi, "state_class", "measurement");
    cJSON_AddStringToObject(rssi, "entity_category", "diagnostic");
    addCameraAvailability(rssi, m_StatusTopic, camera_state_topic);

    const std::string component_payload = jsonString(components);
    cJSON_Delete(components);
    const std::string topic = "homeassistant/device/" + device_id + "/config";
    publishDiscoveryDevice(topic, device_id, camera->getName(), hub_id, component_payload);
    current_ids.push_back(device_id);
  }

  for (const auto &old_id : m_DiscoveredCameraIDs) {
    if (std::find(current_ids.begin(), current_ids.end(), old_id) == current_ids.end()) {
      publish("homeassistant/device/" + old_id + "/config", {}, 1, true);
    }
  }
  m_DiscoveredCameraIDs = current_ids;
}

void MQTT::clearDiscoveryRecords(void) {
  publish(m_HomeAssistantTopic, {}, 1, true);
  for (const auto &camera_id : m_DiscoveredCameraIDs) {
    publish("homeassistant/device/" + camera_id + "/config", {}, 1, true);
  }
  m_DiscoveredCameraIDs.clear();
}

}  // namespace Furble
