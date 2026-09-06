// Hardware-facing doubles for the companion host scenario. The production
// CompanionService, Control, Camera and Fujifilm protocol remain linked; these
// small surfaces replace only NVS, UI battery reads, GPS task scheduling,
// feedback hardware and the ESP timer API.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "freertos/FreeRTOS.h"

#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "esp_timer.h"

const char *LOG_TAG = "furble-host";

namespace Furble {

namespace {

const std::unordered_map<Settings::type_t, Settings::setting_t> SETTINGS = {
    {Settings::BRIGHTNESS,         {Settings::BRIGHTNESS, 1, "Brightness", "brightness", "furble"}        },
    {Settings::TX_POWER,           {Settings::TX_POWER, 4, "TX Power", "tx_power", "furble"}              },
    {Settings::IMU,                {Settings::IMU, 45, "IMU", "imu", "furble"}                            },
    {Settings::IMU_WAKE,           {Settings::IMU_WAKE, 63, "Wake Gesture", "imu_wake", "furble"}         },
    {Settings::IMU_TRIG,           {Settings::IMU_TRIG, 64, "Double-Tap Shutter", "imu_trigger", "furble"}},
    {Settings::TX_ADAPTIVE,        {Settings::TX_ADAPTIVE, 28, "Adaptive", "tx_adaptive", "furble"}       },
    {Settings::MULTICONNECT,
     {Settings::MULTICONNECT, 8, "Multi-Connect", "multiconnect", "furble"}                               },
    {Settings::RECONNECT,          {Settings::RECONNECT, 9, "Infinite-ReConnect", "reconnect", "furble"}  },
    {Settings::RECON_BACKOFF,
     {Settings::RECON_BACKOFF, 16, "Reconnect Backoff", "recon_backoff", "furble"}                        },
    {Settings::SLEEP_CONN,
     {Settings::SLEEP_CONN, 20, "Sleep while connected", "sleep_conn", "furble"}                          },
    {Settings::COMPANION,          {Settings::COMPANION, 12, "Companion", "companion", "furble"}          },
    {Settings::COMPANION_PASSWORD,
     {Settings::COMPANION_PASSWORD, 47, "Companion password", "companion_pw", "furble"}                   },
    {Settings::GPS_PLATFORM,       {Settings::GPS_PLATFORM, 69, "GPS Platform", "gps_plat", "furble"}     },
    {Settings::CONN_SAVER,
     {Settings::CONN_SAVER, 29, "Connection power save", "conn_saver", "furble"}                          },
};

struct BatteryState {
  int32_t level = 0;
  int16_t voltage = 0;
  int32_t current = 0;
  int16_t vbus = 0;
  bool charging = false;
};

std::mutex g_BatteryMutex;
BatteryState g_Battery;

}  // namespace

// Settings ------------------------------------------------------------------

const Settings::setting_t &Settings::get(type_t type) {
  return SETTINGS.at(type);
}

const Settings::setting_t *Settings::getByWireId(uint8_t wire_id) {
  for (const auto &entry : SETTINGS) {
    if (entry.second.wire_id == wire_id) {
      return &entry.second;
    }
  }
  return nullptr;
}

const std::unordered_map<Settings::type_t, Settings::setting_t> &Settings::all(void) {
  return SETTINGS;
}

bool Settings::appliesImmediately(type_t type) {
  return type != BRIGHTNESS;
}

bool Settings::isDangerous(type_t type) {
  return type == TX_POWER || type == TX_ADAPTIVE || type == SLEEP_CONN || type == COMPANION;
}

// Platform and power --------------------------------------------------------

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

uint32_t Platform::tick(void) const {
  return xTaskGetTickCount();
}

void Platform::watchdogFeed(void) {}

Power &Power::getInstance() {
  static Power instance;
  return instance;
}

void Power::acquire(LockType type, const char *) {
  m_Counts[static_cast<size_t>(type)].fetch_add(1);
}

void Power::release(LockType type, const char *) {
  m_Counts[static_cast<size_t>(type)].fetch_sub(1);
}

uint32_t Power::getCount(LockType type) const {
  return m_Counts[static_cast<size_t>(type)].load();
}

// UI status -----------------------------------------------------------------

void Host::setBatteryStatus(int32_t level,
                            int16_t voltage,
                            int32_t current,
                            int16_t vbus,
                            bool charging) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  g_Battery = {level, voltage, current, vbus, charging};
}

int32_t UI::getBatteryLevel(void) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  return g_Battery.level;
}

int16_t UI::getBatteryVoltage(void) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  return g_Battery.voltage;
}

int32_t UI::getBatteryCurrent(void) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  return g_Battery.current;
}

int16_t UI::getBatteryVBUSVoltage(void) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  return g_Battery.vbus;
}

bool UI::isBatteryCharging(void) {
  const std::lock_guard<std::mutex> lock(g_BatteryMutex);
  return g_Battery.charging;
}

uint8_t UI::getIntervalometerState(void) {
  return 0;
}

uint16_t UI::getIntervalometerRemaining(void) {
  return 0;
}

// Feedback ------------------------------------------------------------------

Feedback &Feedback::getInstance() {
  static Feedback instance;
  return instance;
}

void Feedback::reload(void) {}

// GPS -----------------------------------------------------------------------

GPS &GPS::getInstance() {
  static GPS instance;
  return instance;
}

bool GPS::setExternalFix(const external_fix_t &fix) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_ExternalFix = fix;
  m_Source = SOURCE_COMPANION;
  m_Satellites = static_cast<uint8_t>(std::min<unsigned int>(fix.gps.satellites, 255));
  m_HaveExternalFix = true;
  return true;
}

void GPS::clearExternalFix(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_ExternalFix = {};
  m_Source = SOURCE_NONE;
  m_Satellites = 0;
  m_HaveExternalFix = false;
}

void GPS::update(void) {
  external_fix_t fix = {};
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_HaveExternalFix || !m_ExternalFix.position_valid || !m_ExternalFix.time_valid) {
      return;
    }
    fix = m_ExternalFix;
  }
  Control::getInstance().updateGPS(fix.gps, fix.timesync);
}

GPS::source_t GPS::getSource(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Source;
}

uint8_t GPS::getSatellites(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Satellites;
}

GPS::external_fix_t GPS::getExternalFix(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_ExternalFix;
}

}  // namespace Furble

// ESP timer -----------------------------------------------------------------

struct FurbleHostTimer {
  esp_timer_create_args_t args = {};
  bool active = false;
};

std::mutex g_TimerMutex;
FurbleHostTimer *g_ActiveTimer = nullptr;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle) {
  if (args == nullptr || out_handle == nullptr || args->callback == nullptr) {
    return -1;
  }
  auto *timer = new FurbleHostTimer;
  timer->args = *args;
  {
    const std::lock_guard<std::mutex> lock(g_TimerMutex);
    g_ActiveTimer = timer;
  }
  *out_handle = timer;
  return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
  if (handle == nullptr) {
    return -1;
  }
  {
    const std::lock_guard<std::mutex> lock(g_TimerMutex);
    if (g_ActiveTimer == handle) {
      g_ActiveTimer = nullptr;
    }
  }
  delete handle;
  return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t handle) {
  if (handle == nullptr) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(g_TimerMutex);
  return handle->active;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
  (void)timeout_us;
  if (handle == nullptr) {
    return -1;
  }
  const std::lock_guard<std::mutex> lock(g_TimerMutex);
  handle->active = true;
  g_ActiveTimer = handle;
  return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
  if (handle == nullptr) {
    return -1;
  }
  const std::lock_guard<std::mutex> lock(g_TimerMutex);
  handle->active = false;
  return ESP_OK;
}

extern "C" bool furble_host_fire_active_timer(void) {
  esp_timer_cb_t callback = nullptr;
  void *arg = nullptr;
  {
    const std::lock_guard<std::mutex> lock(g_TimerMutex);
    if (g_ActiveTimer == nullptr || !g_ActiveTimer->active) {
      return false;
    }
    g_ActiveTimer->active = false;
    callback = g_ActiveTimer->args.callback;
    arg = g_ActiveTimer->args.arg;
  }
  callback(arg);
  return true;
}
