#include "mqtt_host_dependencies.h"

#include <chrono>

#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace {

esp_netif_t g_Netif = {false, 0};
bool g_HasNetif = false;

}  // namespace

namespace Furble {

Control &Control::getInstance(void) {
  static Control control;
  return control;
}

BaseType_t Control::sendCommand(cmd_t command) {
  m_Commands.push_back(command);
  return pdTRUE;
}

void Control::reset(void) {
  auto &control = getInstance();
  control.m_State = STATE_ACTIVE;
  control.m_Targets.clear();
  m_Commands.clear();
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
};

extern "C" int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                      esp_timer_handle_t *out_handle) {
  if ((args == nullptr) || (out_handle == nullptr)) {
    return ESP_FAIL;
  }
  *out_handle = new esp_timer_stub_t {*args};
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t) {
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t) {
  return ESP_OK;
}

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
