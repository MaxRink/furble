#include "FurbleWiFi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <time.h>

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "Preferences.h"

namespace Furble {

namespace {

constexpr const char *LOG_TAG = "wifi";
constexpr const char *BSSID_KEY = "wifi_bssid";
constexpr const char *CHANNEL_KEY = "wifi_chan";
constexpr size_t BSSID_LENGTH = 6;

constexpr uint32_t NOTIFY_REQUEST = 1U << 0;
constexpr uint32_t NOTIFY_EVENT = 1U << 1;
constexpr uint32_t NOTIFY_STOP = 1U << 2;

constexpr uint32_t RETRY_INITIAL_MS = 1000;
constexpr uint32_t RETRY_MAX_MS = 60 * 1000;
constexpr size_t TASK_STACK = 4096;

esp_netif_t *g_StaNetif = nullptr;
TaskHandle_t g_Task = nullptr;
std::mutex g_StateMutex;
std::mutex g_DriverMutex;
std::mutex g_NtpMutex;

bool g_Initialized = false;
std::atomic<bool> g_DriverInitialized = false;
std::atomic<bool> g_Connected = false;
std::atomic<bool> g_GotIp = false;
bool g_BssidSet = false;
std::array<uint8_t, BSSID_LENGTH> g_Bssid = {};
uint8_t g_Channel = 0;
int8_t g_Rssi = 0;
std::string g_Ip;

std::atomic<bool> g_ConnectRequested = false;
uint32_t g_RetryDelayMs = 0;
uint32_t g_DirectFailures = 0;
bool g_ScanAttempted = false;
std::atomic<bool> g_ScanBlocked = false;

bool g_NtpInitialized = false;
std::atomic<bool> g_NtpRunning = false;
bool g_NtpSynced = false;
time_t g_NtpLastSync = 0;
int64_t g_NtpOffsetUs = 0;
int64_t g_SyncStartWallUs = 0;
int64_t g_SyncStartMonoUs = 0;

void notify(uint32_t value) {
  if (g_Task != nullptr) {
    xTaskNotify(g_Task, value, eSetBits);
  }
}

void loadRememberedAccessPoint(void) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, true)) {
    return;
  }

  if (preferences.isKey(BSSID_KEY) && (preferences.getBytesLength(BSSID_KEY) == BSSID_LENGTH)) {
    g_StateMutex.lock();
    preferences.get(BSSID_KEY, g_Bssid.data(), g_Bssid.size());
    g_BssidSet = true;
    g_StateMutex.unlock();
  }
  if (preferences.isKey(CHANNEL_KEY)) {
    g_StateMutex.lock();
    g_Channel = preferences.get<uint8_t>(CHANNEL_KEY, 0);
    g_StateMutex.unlock();
  }

  preferences.end();
}

void saveRememberedAccessPoint(const wifi_ap_record_t &record) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, false)) {
    return;
  }

  preferences.put(BSSID_KEY, record.bssid, BSSID_LENGTH);
  preferences.put<uint8_t>(CHANNEL_KEY, record.primary);
  preferences.end();

  std::lock_guard<std::mutex> lock(g_StateMutex);
  std::memcpy(g_Bssid.data(), record.bssid, BSSID_LENGTH);
  g_BssidSet = true;
  g_Channel = record.primary;
  g_Rssi = record.rssi;
}

void clearStoredAccessPoint(void) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, false)) {
    return;
  }

  if (preferences.isKey(BSSID_KEY)) {
    preferences.remove(BSSID_KEY);
  }
  if (preferences.isKey(CHANNEL_KEY)) {
    preferences.remove(CHANNEL_KEY);
  }
  preferences.end();

  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_Bssid = {};
  g_BssidSet = false;
  g_Channel = 0;
  g_Rssi = 0;
}

void timeSyncCallback(struct timeval *tv) {
  if (tv == nullptr) {
    return;
  }

  const int64_t syncedWallUs = (static_cast<int64_t>(tv->tv_sec) * 1000000LL) + tv->tv_usec;
  const int64_t elapsedUs = esp_timer_get_time() - g_SyncStartMonoUs;
  const int64_t expectedWallUs = g_SyncStartWallUs + elapsedUs;

  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_NtpSynced = true;
  g_NtpLastSync = tv->tv_sec;
  g_NtpOffsetUs = syncedWallUs - expectedWallUs;
}

void stopNtp(void) {
  std::lock_guard<std::mutex> lock(g_NtpMutex);

  if (g_NtpInitialized) {
    esp_netif_sntp_deinit();
    g_NtpInitialized = false;
  }
  g_NtpRunning = false;
}

bool startNtp(void) {
  if (!g_GotIp || !Settings::load<Settings::NTP>()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_NtpMutex);
  if (g_NtpRunning) {
    return true;
  }

  const std::string server = Settings::load<Settings::NTP_SERVER>();
  if (server.empty()) {
    ESP_LOGE(LOG_TAG, "NTP server is empty");
    return false;
  }

  timeval before;
  gettimeofday(&before, nullptr);
  g_SyncStartWallUs = (static_cast<int64_t>(before.tv_sec) * 1000000LL) + before.tv_usec;
  g_SyncStartMonoUs = esp_timer_get_time();

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server.c_str());
  config.start = false;
  config.smooth_sync = false;
  config.sync_cb = timeSyncCallback;

  esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "NTP init failed: %s", esp_err_to_name(err));
    return false;
  }
  g_NtpInitialized = true;

  err = esp_netif_sntp_start();
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "NTP start failed: %s", esp_err_to_name(err));
    esp_netif_sntp_deinit();
    g_NtpInitialized = false;
    return false;
  }

  g_NtpRunning = true;
  ESP_LOGI(LOG_TAG, "NTP started with %s", server.c_str());
  return true;
}

bool configureStation(void) {
  const std::string ssid = Settings::load<Settings::WIFI_SSID>();
  const std::string psk = Settings::load<Settings::WIFI_PSK>();
  if (ssid.empty()) {
    ESP_LOGE(LOG_TAG, "WiFi SSID is empty");
    return false;
  }
  wifi_config_t config = {};
  if ((ssid.size() > sizeof(config.sta.ssid)) || (psk.size() > sizeof(config.sta.password))) {
    ESP_LOGE(LOG_TAG, "WiFi credentials are too long");
    return false;
  }

  std::memcpy(config.sta.ssid, ssid.data(), ssid.size());
  std::memcpy(config.sta.password, psk.data(), psk.size());

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    if (g_BssidSet && (g_Channel != 0)) {
      std::memcpy(config.sta.bssid, g_Bssid.data(), BSSID_LENGTH);
      config.sta.bssid_set = true;
      config.sta.channel = g_Channel;
    }
  }

  const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "WiFi config failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool startDriver(void) {
  std::lock_guard<std::mutex> lock(g_DriverMutex);
  if (g_DriverInitialized) {
    return true;
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "WiFi init failed: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err == ESP_OK) {
    err = esp_wifi_set_mode(WIFI_MODE_STA);
  }
  if (err == ESP_OK) {
    err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  }
  if (err == ESP_OK) {
    err = esp_wifi_start();
  }
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "WiFi start failed: %s", esp_err_to_name(err));
    esp_wifi_deinit();
    return false;
  }

  g_DriverInitialized = true;
  ESP_LOGI(LOG_TAG, "WiFi station started");
  return true;
}

void stopDriver(void) {
  std::lock_guard<std::mutex> lock(g_DriverMutex);
  if (!g_DriverInitialized) {
    return;
  }

  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  g_DriverInitialized = false;
}

void recordFailure(void) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_DirectFailures++;
  if (g_RetryDelayMs == 0) {
    g_RetryDelayMs = RETRY_INITIAL_MS;
  } else {
    g_RetryDelayMs = std::min(g_RetryDelayMs * 2, RETRY_MAX_MS);
  }
}

uint32_t takeRetryDelay(void) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  const uint32_t delay = g_RetryDelayMs;
  if (delay == 0) {
    return 0;
  }

  const uint32_t jitter = esp_random() % (delay / 4 + 1);
  return delay + jitter;
}

bool scanForNetwork(void) {
  if (Control::getInstance().getState() == Control::STATE_ACTIVE) {
    ESP_LOGW(LOG_TAG, "refusing to scan while a camera is connected");
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ScanBlocked = true;
    return false;
  }

  const std::string ssid = Settings::load<Settings::WIFI_SSID>();
  wifi_scan_config_t config = {};
  config.show_hidden = true;
  esp_err_t err = esp_wifi_scan_start(&config, true);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "WiFi scan failed: %s", esp_err_to_name(err));
    return false;
  }

  uint16_t count = 0;
  err = esp_wifi_scan_get_ap_num(&count);
  if ((err != ESP_OK) || (count == 0)) {
    ESP_LOGW(LOG_TAG, "WiFi scan found no access points");
    return false;
  }

  std::vector<wifi_ap_record_t> records(count);
  err = esp_wifi_scan_get_ap_records(&count, records.data());
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "WiFi scan results failed: %s", esp_err_to_name(err));
    return false;
  }

  for (const auto &record : records) {
    const size_t recordLength =
        strnlen(reinterpret_cast<const char *>(record.ssid), sizeof(record.ssid));
    if ((recordLength == ssid.size())
        && (std::memcmp(record.ssid, ssid.data(), recordLength) == 0)) {
      saveRememberedAccessPoint(record);
      ESP_LOGI(LOG_TAG, "WiFi found saved access point on channel %u",
               static_cast<unsigned>(record.primary));
      return true;
    }
  }

  ESP_LOGW(LOG_TAG, "WiFi scan did not find SSID %s", ssid.c_str());
  return false;
}

bool connectAttempt(void) {
  if (!startDriver()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_DriverMutex);
  if (!g_ConnectRequested || !g_DriverInitialized) {
    return false;
  }
  if (!configureStation()) {
    return false;
  }

  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "WiFi connect request failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(LOG_TAG, "WiFi connection requested");
  return true;
}

void wifiTask(void *) {
  while (true) {
    uint32_t notification = 0;
    xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);

    while (g_ConnectRequested && !g_Connected) {
      {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        if (g_ScanBlocked) {
          break;
        }
      }

      const uint32_t delay = takeRetryDelay();
      if (delay > 0) {
        xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(delay));
        if (!g_ConnectRequested || g_Connected) {
          break;
        }
      }

      bool scan = false;
      {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        scan = (g_DirectFailures >= 2) && !g_ScanAttempted;
        if (scan) {
          g_ScanAttempted = true;
        }
      }
      if (scan && !scanForNetwork()) {
        if (g_ScanBlocked) {
          break;
        }
      }

      if (!connectAttempt()) {
        recordFailure();
        notify(NOTIFY_EVENT);
        continue;
      }

      xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);
      if (!g_ConnectRequested || g_Connected) {
        break;
      }
    }
  }
}

void eventHandler(void *, esp_event_base_t eventBase, int32_t eventId, void *) {
  if ((eventBase == WIFI_EVENT) && (eventId == WIFI_EVENT_STA_DISCONNECTED)) {
    stopNtp();
    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_Connected = false;
      g_GotIp = false;
      g_Ip.clear();
      g_Rssi = 0;
    }
    if (g_ConnectRequested) {
      recordFailure();
      ESP_LOGW(LOG_TAG, "WiFi disconnected, retrying with backoff");
    }
    notify(NOTIFY_EVENT);
    return;
  }

  if ((eventBase == IP_EVENT) && (eventId == IP_EVENT_STA_GOT_IP)) {
    wifi_ap_record_t record = {};
    if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
      saveRememberedAccessPoint(record);
    }

    char ip[16] = {};
    if (g_StaNetif != nullptr) {
      esp_netif_ip_info_t info = {};
      if (esp_netif_get_ip_info(g_StaNetif, &info) == ESP_OK) {
        esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
      }
    }

    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_Connected = true;
      g_GotIp = true;
      g_Ip = ip;
      g_DirectFailures = 0;
      g_RetryDelayMs = 0;
      g_ScanAttempted = false;
      g_ScanBlocked = false;
    }
    ESP_LOGI(LOG_TAG, "WiFi connected, ip=%s", ip);
    if (Settings::load<Settings::NTP>()) {
      startNtp();
    }
    notify(NOTIFY_EVENT);
  }
}

}  // namespace

void WiFi::init(void) {
  if (g_Initialized) {
    return;
  }

  g_StaNetif = esp_netif_create_default_wifi_sta();
  if (g_StaNetif == nullptr) {
    ESP_LOGE(LOG_TAG, "Failed to create station netif");
    abort();
  }

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &eventHandler, nullptr));
  ESP_ERROR_CHECK(
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &eventHandler, nullptr));

  loadRememberedAccessPoint();
  BaseType_t result = xTaskCreate(wifiTask, "wifi", TASK_STACK, nullptr, 2, &g_Task);
  if (result != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create WiFi task");
    abort();
  }

  g_Initialized = true;
  if (Settings::load<Settings::WIFI>()) {
    WiFi::connect();
  }
}

bool WiFi::connect(void) {
  if (!g_Initialized || !Settings::load<Settings::WIFI>()
      || Settings::load<Settings::WIFI_SSID>().empty()) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ConnectRequested = true;
    g_RetryDelayMs = 0;
    g_DirectFailures = 0;
    g_ScanAttempted = false;
    g_ScanBlocked = false;
  }
  notify(NOTIFY_REQUEST);
  return true;
}

void WiFi::disconnect(void) {
  g_ConnectRequested = false;
  stopNtp();
  notify(NOTIFY_STOP);

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_Connected = false;
    g_GotIp = false;
    g_Ip.clear();
    g_Rssi = 0;
  }
  stopDriver();
}

bool WiFi::setEnabled(bool enabled) {
  if (enabled) {
    return WiFi::connect();
  }

  WiFi::disconnect();
  return true;
}

void WiFi::forget(void) {
  WiFi::disconnect();
  Settings::save<Settings::WIFI_SSID>("");
  Settings::save<Settings::WIFI_PSK>("");
  clearStoredAccessPoint();
}

void WiFi::clearRememberedAccessPoint(void) {
  clearStoredAccessPoint();
}

bool WiFi::setNtpEnabled(bool enabled) {
  if (!enabled) {
    stopNtp();
    return true;
  }

  if (!g_GotIp) {
    return true;
  }
  return startNtp();
}

bool WiFi::reloadNtp(void) {
  stopNtp();
  if (!Settings::load<Settings::NTP>() || !g_GotIp) {
    return true;
  }
  return startNtp();
}

bool WiFi::syncNtp(void) {
  if (!Settings::load<Settings::NTP>() || !g_GotIp) {
    return false;
  }

  stopNtp();
  return startNtp();
}

WiFi::status_t WiFi::getStatus(void) {
  status_t status = {};
  status.enabled = Settings::load<Settings::WIFI>();
  status.ssid = Settings::load<Settings::WIFI_SSID>();

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    status.driver = g_DriverInitialized;
    status.connected = g_Connected;
    status.bssid = g_Bssid;
    status.bssid_set = g_BssidSet;
    status.channel = g_Channel;
    status.rssi = g_Rssi;
    status.ip = g_Ip;
    status.ntp_running = g_NtpRunning;
    status.ntp_synced = g_NtpSynced;
    status.ntp_last_sync = g_NtpLastSync;
    status.ntp_offset_us = g_NtpOffsetUs;
  }
  status.ntp_enabled = Settings::load<Settings::NTP>();

  if (!status.enabled && !status.driver) {
    status.state = STATE_DISABLED;
  } else if (status.connected) {
    status.state = STATE_CONNECTED;
  } else if (status.driver) {
    status.state = STATE_CONNECTING;
  } else {
    status.state = STATE_IDLE;
  }

  return status;
}

bool WiFi::getNtpTimesync(Camera::timesync_t &timesync) {
  bool synced = false;
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    synced = g_NtpSynced;
  }
  if (!synced) {
    return false;
  }

  timeval now = {};
  gettimeofday(&now, nullptr);
  tm utc = {};
  if (gmtime_r(&now.tv_sec, &utc) == nullptr) {
    return false;
  }

  timesync.year = static_cast<unsigned int>(utc.tm_year + 1900);
  timesync.month = static_cast<unsigned int>(utc.tm_mon + 1);
  timesync.day = static_cast<unsigned int>(utc.tm_mday);
  timesync.hour = static_cast<unsigned int>(utc.tm_hour);
  timesync.minute = static_cast<unsigned int>(utc.tm_min);
  timesync.second = static_cast<unsigned int>(utc.tm_sec);
  timesync.centisecond = static_cast<unsigned int>(now.tv_usec / 10000);
  return true;
}

}  // namespace Furble
