#include "mqtt_host_dependencies.h"

#include <chrono>
#include <cstring>
#include <deque>
#include <vector>

#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

struct HostQueue {
  size_t itemSize;
  size_t capacity;
  std::deque<std::vector<uint8_t>> items;
};

esp_netif_t g_Netif = {false, 0};
bool g_HasNetif = false;

}  // namespace

namespace Furble {

Control &Control::getInstance(void) {
  static Control control;
  return control;
}

BaseType_t Control::sendCommand(cmd_t command) {
  if ((command == CMD_SHUTTER_RELEASE || command == CMD_FOCUS_RELEASE) && m_FailNextRelease) {
    m_FailNextRelease = false;
    return pdFALSE;
  }
  m_Commands.push_back(command);
  return pdTRUE;
}

void Control::reset(void) {
  auto &control = getInstance();
  control.m_State = STATE_ACTIVE;
  control.m_Targets.clear();
  m_Commands.clear();
  m_FailNextRelease = false;
}

void Control::setState(state_t state) {
  getInstance().m_State = state;
}

const std::vector<Control::cmd_t> &Control::commands(void) {
  return m_Commands;
}

void Control::setTargetStatus(const std::vector<target_status_t> &targets) {
  getInstance().m_Targets = targets;
}

Platform &Platform::getInstance(void) {
  static Platform platform;
  return platform;
}

void Platform::setBattery(const battery_t &battery) {
  getInstance().m_Battery = battery;
}

GPS &GPS::getInstance(void) {
  static GPS gps;
  return gps;
}

bool GPS::getCurrentFix(external_fix_t &fix) const {
  if (!m_HaveFix) {
    return false;
  }
  fix = m_Fix;
  return true;
}

void GPS::reset(void) {
  auto &gps = getInstance();
  gps.m_Fix = {};
  gps.m_Source = SOURCE_NONE;
  gps.m_HaveFix = false;
}

bool Settings::mqttEnabled = true;
bool Settings::mqttHA = true;
bool Settings::gpsEnabled = false;
bool Settings::reconnect = false;
std::string Settings::mqttURI = "mqtt://loopback";
std::string Settings::mqttUser;
std::string Settings::mqttPassword;
std::string Settings::mqttBase = "furble";
interval_t Settings::interval = {
    {1, SpinValue::UNIT_MS},
    {1, SpinValue::UNIT_MS},
    {1, SpinValue::UNIT_MS},
    {1, SpinValue::UNIT_MS},
};

void Settings::reset(void) {
  mqttEnabled = true;
  mqttHA = true;
  gpsEnabled = false;
  reconnect = false;
  mqttURI = "mqtt://loopback";
  mqttUser.clear();
  mqttPassword.clear();
  mqttBase = "furble";
  interval = {
      {1, SpinValue::UNIT_MS},
      {1, SpinValue::UNIT_MS},
      {1, SpinValue::UNIT_MS},
      {1, SpinValue::UNIT_MS},
  };
}

}  // namespace Furble

namespace host_mqtt_network {

void reset(void) {
  g_Netif = {false, 0};
  g_HasNetif = false;
}

void setGotIp(const char *, uint32_t address) {
  g_Netif.up = address != 0;
  g_Netif.ip = address;
  g_HasNetif = true;
}

}  // namespace host_mqtt_network

extern "C" esp_netif_t *esp_netif_next_unsafe(esp_netif_t *netif) {
  if (!g_HasNetif || (netif != nullptr)) {
    return nullptr;
  }
  return &g_Netif;
}

extern "C" bool esp_netif_is_netif_up(esp_netif_t *netif) {
  return netif != nullptr && netif->up;
}

extern "C" esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
  if ((netif == nullptr) || (info == nullptr)) {
    return ESP_FAIL;
  }
  info->ip.addr = netif->ip;
  return ESP_OK;
}

struct esp_timer_stub_t {
  esp_timer_create_args_t args;
  bool active = false;
};

std::vector<esp_timer_handle_t> g_Timers;
int64_t g_TimerOffsetUs = 0;
uint32_t g_TaskNotifications = 0;

extern "C" int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() + g_TimerOffsetUs;
}

extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                      esp_timer_handle_t *out_handle) {
  if ((args == nullptr) || (out_handle == nullptr)) {
    return ESP_FAIL;
  }
  *out_handle = new esp_timer_stub_t {*args, false};
  g_Timers.push_back(*out_handle);
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t) {
  if (handle != nullptr) {
    handle->active = true;
  }
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
  if (handle != nullptr) {
    handle->active = false;
  }
  return ESP_OK;
}

namespace host_mqtt_timer {

void reset(void) {
  g_TimerOffsetUs = 0;
  g_TaskNotifications = 0;
  for (const auto handle : g_Timers) {
    if (handle != nullptr) {
      handle->active = false;
    }
  }
}

void fireAll(void) {
  const auto timers = g_Timers;
  for (const auto handle : timers) {
    if ((handle != nullptr) && handle->active) {
      handle->active = false;
      handle->args.callback(handle->args.arg);
    }
  }
}

void advance(uint64_t milliseconds) {
  g_TimerOffsetUs += static_cast<int64_t>(milliseconds) * 1000;
}

uint32_t notifications(void) {
  return g_TaskNotifications;
}

}  // namespace host_mqtt_timer

extern "C" BaseType_t xTaskCreate(void (*)(void *),
                                  const char *,
                                  uint32_t,
                                  void *,
                                  UBaseType_t,
                                  TaskHandle_t *out_handle) {
  if (out_handle != nullptr) {
    *out_handle = reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(1));
  }
  return pdPASS;
}

extern "C" void vTaskDelay(TickType_t) {}

extern "C" BaseType_t xTaskNotifyGive(TaskHandle_t task) {
  if (task != nullptr) {
    g_TaskNotifications++;
  }
  return pdTRUE;
}

extern "C" uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) {
  return 0;
}

extern "C" QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  if ((length == 0) || (item_size == 0)) {
    return nullptr;
  }
  return new HostQueue {item_size, length, {}};
}

extern "C" BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
  auto *hostQueue = static_cast<HostQueue *>(queue);
  if ((hostQueue == nullptr) || (item == nullptr)
      || (hostQueue->items.size() >= hostQueue->capacity)) {
    return pdFALSE;
  }
  std::vector<uint8_t> copy(hostQueue->itemSize);
  std::memcpy(copy.data(), item, hostQueue->itemSize);
  hostQueue->items.push_back(std::move(copy));
  return pdTRUE;
}

extern "C" BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t) {
  auto *hostQueue = static_cast<HostQueue *>(queue);
  if ((hostQueue == nullptr) || (item == nullptr) || hostQueue->items.empty()) {
    return pdFALSE;
  }
  const auto &copy = hostQueue->items.front();
  std::memcpy(item, copy.data(), hostQueue->itemSize);
  hostQueue->items.pop_front();
  return pdTRUE;
}
