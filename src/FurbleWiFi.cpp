#include "FurbleWiFi.h"
#include "FurbleEthernet.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#if defined(FURBLE_WIFI_HOST_TEST)
#include <chrono>
#endif
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

#include "FurbleBootTimeline.h"
#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleWiFiPolicy.h"
#include "Preferences.h"

namespace Furble {

namespace {

[[maybe_unused]] constexpr const char *LOG_TAG = "wifi";
constexpr const char *BSSID_KEY = "wifi_bssid";
constexpr const char *CHANNEL_KEY = "wifi_chan";
constexpr const char *AUTH_KEY = "wifi_auth";
constexpr size_t BSSID_LENGTH = 6;

constexpr uint32_t NOTIFY_REQUEST = 1U << 0;
constexpr uint32_t NOTIFY_EVENT = 1U << 1;
constexpr uint32_t NOTIFY_STOP = 1U << 2;

constexpr uint32_t RETRY_INITIAL_MS = 1000;
// A transient outage retries long and slow; the old 60 s cap kept an idle
// station waking every minute forever during a shoot. Permanent failures stop
// instead of retrying at all.
constexpr uint32_t RETRY_MAX_MS = 300 * 1000;
// Bounded attempts for reasons that may clear on their own. A router reboot
// reconnects; a wrong passphrase does not and is stopped immediately.
constexpr uint32_t NO_AP_MAX_ATTEMPTS = 6;
constexpr uint32_t TRANSIENT_MAX_ATTEMPTS = 10;
// Recheck cadence while a wired address owns the network, and while connected
// so the station can stop when the wire comes back.
constexpr uint32_t WIRED_RECHECK_MS = 5000;
// While a camera session blocks the join scan, retry soon after it ends
// instead of waiting out the full reconnect backoff.
constexpr uint32_t COEX_RETRY_MS = 10 * 1000;
constexpr size_t TASK_STACK = 4096;

esp_netif_t *g_StaNetif = nullptr;
TaskHandle_t g_Task = nullptr;
std::mutex g_StateMutex;
// Serializes the stored access point. Every NVS save or erase of it runs
// under this mutex, always taken before g_StateMutex.
std::mutex g_ApStorageMutex;
// Bumped by each clear, under both mutexes. A save captures it when it reads
// the configuration it saves for, and is dropped if a clear ran since, so a
// save that started before a clear can never land after it.
uint32_t g_ApGeneration = 0;
std::mutex g_DriverMutex;
// First-time initialization creates a netif, event handlers, and a task. Two
// console/companion/WebUI callers at once must not create them twice.
std::mutex g_InitMutex;
std::mutex g_NtpMutex;

bool g_Initialized = false;
std::atomic<bool> g_DriverInitialized = false;
std::atomic<bool> g_Connected = false;
std::atomic<bool> g_GotIp = false;
bool g_BssidSet = false;
std::array<uint8_t, BSSID_LENGTH> g_Bssid = {};
uint8_t g_Channel = 0;
bool g_AuthSet = false;
uint8_t g_AuthMode = WiFiPolicy::AUTH_UNKNOWN;
int8_t g_Rssi = 0;
std::string g_Ip;

std::atomic<bool> g_ConnectRequested = false;
uint32_t g_RetryDelayMs = 0;
uint32_t g_DirectFailures = 0;
uint32_t g_ConnectAttempts = 0;
std::atomic<bool> g_StoppedForReason {false};
std::atomic<bool> g_DeferredForWired {false};
// True when the deferral came from an automatic request or a live connection.
// A manual `wifi disconnect` clears it, so losing the wire later does not
// silently reconnect a station the operator stopped.
std::atomic<bool> g_ResumeAfterWired {false};
uint8_t g_LastDisconnectReason = 0;
bool g_ScanAttempted = false;
// A direct join to the remembered BSSID failed. Force one scan before the next
// join so a moved or replaced access point is not retried for the whole backoff.
bool g_FastConnectFailed = false;
std::atomic<bool> g_LastAttemptUsedBssid {false};
std::atomic<bool> g_ScanRefusedForCoex {false};
// Incremented by every disconnect and GOT_IP event. The join wait uses it to
// distinguish a real lifecycle event from the periodic cancellation poll, so
// a slow association is not mistaken for a dead attempt.
std::atomic<uint32_t> g_LifecycleEvent {0};

// Time budget for one asynchronous join. Slow association or DHCP may exceed
// the cancellation poll; only an explicit deadline starts another attempt.
constexpr uint32_t JOIN_DEADLINE_MS = 15000;

int64_t wifiNowUs(void) {
#if defined(FURBLE_WIFI_HOST_TEST)
  // The host stubs return from the wait immediately and leave esp_timer fixed,
  // so backoff deadlines must use a clock that advances.
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
#else
  return esp_timer_get_time();
#endif
}

// A join scan shares the radio with the camera link. Only run it while Control
// is idle: during connect, active, and disconnecting the camera session owns
// the arbitration window.
bool joinScanAllowed(Control::state_t state) {
  return state == Control::STATE_IDLE;
}

enum class DisconnectClass : uint8_t {
  NONE,
  AUTH,
  NO_AP,
  TRANSIENT,
};

bool wiredNetworkActive(void) {
  return Ethernet::isConnected();
}

DisconnectClass classifyDisconnect(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
      // A confirmed credential failure. Retrying cannot fix it; the operator
      // must change the setting and run `wifi connect`.
      return DisconnectClass::AUTH;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      // An AP restart, an overloaded AP, or a transient handshake timeout can
      // produce these. Bound the retries instead of declaring the credentials
      // permanently wrong.
      return DisconnectClass::TRANSIENT;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      // Likely a router reboot or out of range. Bounded retries cover it.
      return DisconnectClass::NO_AP;
    default:
      return DisconnectClass::TRANSIENT;
  }
}

bool g_NtpInitialized = false;
std::atomic<bool> g_NtpRunning = false;
std::string g_NtpServer;
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

  bool bssidSet = false;
  std::array<uint8_t, BSSID_LENGTH> bssid = {};
  uint8_t channel = 0;
  bool authSet = false;
  uint8_t authMode = WiFiPolicy::AUTH_UNKNOWN;

  if (preferences.isKey(BSSID_KEY) && (preferences.getBytesLength(BSSID_KEY) == BSSID_LENGTH)) {
    bssidSet = preferences.get(BSSID_KEY, bssid.data(), bssid.size()) == bssid.size();
  }
  if (preferences.isKey(CHANNEL_KEY)) {
    channel = preferences.get<uint8_t>(CHANNEL_KEY, 0);
  }
  if (preferences.isKey(AUTH_KEY)) {
    authMode = preferences.get<uint8_t>(AUTH_KEY, WiFiPolicy::AUTH_UNKNOWN);
    authSet = WiFiPolicy::authUsable(true, authMode);
  }
  preferences.end();

  const std::lock_guard<std::mutex> lock(g_StateMutex);
  g_Bssid = bssid;
  g_BssidSet = bssidSet;
  g_Channel = channel;
  g_AuthMode = authMode;
  g_AuthSet = authSet;
}

void persistAccessPoint(const wifi_ap_record_t &record) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, false)) {
    return;
  }

  preferences.setDeferredCommit(true);
  preferences.put(BSSID_KEY, record.bssid, BSSID_LENGTH);
  preferences.put<uint8_t>(CHANNEL_KEY, record.primary);
  preferences.put<uint8_t>(AUTH_KEY, static_cast<uint8_t>(record.authmode));
  (void)preferences.commit();
  preferences.end();
}

uint32_t apGeneration(void) {
  const std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_ApGeneration;
}

/** The in-memory half of remembering an access point; no NVS. */
void rememberAccessPoint(const wifi_ap_record_t &record, uint32_t generation) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  if (generation != g_ApGeneration) {
    return;
  }
  g_FastConnectFailed = false;
  if (!g_ConnectRequested) {
    return;
  }
  std::memcpy(g_Bssid.data(), record.bssid, BSSID_LENGTH);
  g_BssidSet = true;
  g_Channel = record.primary;
  g_AuthMode = static_cast<uint8_t>(record.authmode);
  g_AuthSet = WiFiPolicy::authUsable(true, g_AuthMode);
  g_Rssi = record.rssi;
}

/** Save and remember record unless a clear ran since generation was read. */
void saveRememberedAccessPoint(const wifi_ap_record_t &record, uint32_t generation) {
  const std::lock_guard<std::mutex> storage(g_ApStorageMutex);
  if (apGeneration() != generation) {
    return;
  }
  persistAccessPoint(record);
  rememberAccessPoint(record, generation);
}

void eraseStoredAccessPoint(void) {
  Preferences preferences;
  if (!preferences.begin(FURBLE_STR, false)) {
    return;
  }

  preferences.setDeferredCommit(true);
  if (preferences.isKey(BSSID_KEY)) {
    preferences.remove(BSSID_KEY);
  }
  if (preferences.isKey(CHANNEL_KEY)) {
    preferences.remove(CHANNEL_KEY);
  }
  if (preferences.isKey(AUTH_KEY)) {
    preferences.remove(AUTH_KEY);
  }
  (void)preferences.commit();
  preferences.end();
}

void cancelPendingApStorage(void);

void clearStoredAccessPoint(void) {
  const std::lock_guard<std::mutex> storage(g_ApStorageMutex);
  cancelPendingApStorage();
  eraseStoredAccessPoint();

  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_Bssid = {};
  g_BssidSet = false;
  g_Channel = 0;
  g_AuthMode = WiFiPolicy::AUTH_UNKNOWN;
  g_AuthSet = false;
  g_Rssi = 0;
  g_FastConnectFailed = false;
  g_LastAttemptUsedBssid.store(false);
}

void timeSyncCallback(struct timeval *tv) {
  if (tv == nullptr) {
    return;
  }

  const int64_t syncedWallUs = (static_cast<int64_t>(tv->tv_sec) * 1000000LL) + tv->tv_usec;
  const int64_t elapsedUs = esp_timer_get_time() - g_SyncStartMonoUs;
  const int64_t expectedWallUs = g_SyncStartWallUs + elapsedUs;

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_NtpSynced = true;
    g_NtpLastSync = tv->tv_sec;
    g_NtpOffsetUs = syncedWallUs - expectedWallUs;
  }
  BootTimeline::mark(BootTimeline::NTP_SYNCED);
}

void stopNtp(void) {
  std::lock_guard<std::mutex> lock(g_NtpMutex);

  if (g_NtpInitialized) {
    esp_netif_sntp_deinit();
    g_NtpInitialized = false;
  }
  g_NtpServer.clear();
  g_NtpRunning = false;
}

bool startNtp(void) {
  if (!g_ConnectRequested || !g_GotIp || !Settings::load<Settings::NTP>()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_NtpMutex);
  if (!g_ConnectRequested || !g_GotIp || !Settings::load<Settings::NTP>()) {
    return false;
  }
  if (g_NtpRunning) {
    return true;
  }

  g_NtpServer = Settings::load<Settings::NTP_SERVER>();
  if (!Settings::validNetworkString(Settings::NTP_SERVER, g_NtpServer)) {
    ESP_LOGE(LOG_TAG, "NTP server is invalid");
    return false;
  }

  timeval before;
  gettimeofday(&before, nullptr);
  g_SyncStartWallUs = (static_cast<int64_t>(before.tv_sec) * 1000000LL) + before.tv_usec;
  g_SyncStartMonoUs = esp_timer_get_time();

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(g_NtpServer.c_str());
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
    g_NtpServer.clear();
    return false;
  }

  g_NtpRunning = true;
  ESP_LOGI(LOG_TAG, "NTP started with %s", g_NtpServer.c_str());
  return true;
}

bool configureStation(void) {
  const std::string ssid = Settings::load<Settings::WIFI_SSID>();
  const std::string psk = Settings::load<Settings::WIFI_PSK>();
  if (!Settings::validNetworkString(Settings::WIFI_SSID, ssid)
      || !Settings::validNetworkString(Settings::WIFI_PSK, psk)) {
    ESP_LOGE(LOG_TAG, "WiFi credentials are invalid");
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
    const WiFiPolicy::remembered_ap_t ap = {g_BssidSet, g_Channel, g_AuthSet, g_AuthMode};
    if (WiFiPolicy::fastConnectUsable(ap)) {
      std::memcpy(config.sta.bssid, g_Bssid.data(), BSSID_LENGTH);
      config.sta.bssid_set = true;
      config.sta.channel = g_Channel;
      // A direct join has no scan to compare candidates, so keep the driver's
      // fast path explicit and reject an AP whose security is weaker than the
      // one remembered beside the BSSID.
      config.sta.scan_method = WIFI_FAST_SCAN;
      config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
      if (WiFiPolicy::authUsable(g_AuthSet, g_AuthMode)) {
        config.sta.threshold.authmode = static_cast<wifi_auth_mode_t>(g_AuthMode);
      }
      g_LastAttemptUsedBssid.store(true);
    } else {
      g_LastAttemptUsedBssid.store(false);
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
  if (!g_ConnectRequested) {
    return false;
  }
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

void stopDriverLocked(void) {
  if (!g_DriverInitialized) {
    return;
  }

  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  g_DriverInitialized = false;
}

void stopDriver(void) {
  std::lock_guard<std::mutex> lock(g_DriverMutex);
  stopDriverLocked();
}

// Hand the station back for a wired address. Clearing the request and
// publishing the resume state happen in one critical section shared with
// WiFi::disconnect(), so an explicit disconnect or disable that lands before
// is visible here and cancels the takeover, and one that lands after clears
// the flags again. Without that serialisation the old code could re-arm the
// resume after a manual disconnect and restart the station on cable loss.
bool deferForWired(void) {
  {
    const std::lock_guard<std::mutex> lock(g_StateMutex);
    if (!g_ConnectRequested.load() && !g_Connected.load()) {
      // Already stopped or cancelled; there is nothing to hand over.
      return false;
    }
    g_ConnectRequested = false;
    g_Connected = false;
    g_GotIp = false;
    g_Ip.clear();
    g_Rssi = 0;
    g_DeferredForWired = true;
    g_ResumeAfterWired = true;
  }
  stopNtp();
  notify(NOTIFY_STOP);
  stopDriver();
  return true;
}

// A terminal disconnect (authentication failure, or both attempt bounds spent)
// clears the request but leaves the station started and the driver
// initialised. Release both so the radio is not left running after the failure
// the operator has to fix. Runs from every path that can observe the terminal
// state, including the connected-state wait that used to `continue` past it.
void serviceTerminalStop(void) {
  if (!g_ConnectRequested && !g_Connected && g_StoppedForReason) {
    stopDriver();
  }
}

void recordFailure(bool scheduleRetry = true) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_DirectFailures++;
  g_ConnectAttempts++;
  if (!scheduleRetry) {
    // A permanent failure is counted but never given a retry deadline.
    g_RetryDelayMs = 0;
    return;
  }
  if (g_RetryDelayMs == 0) {
    g_RetryDelayMs = RETRY_INITIAL_MS;
  } else {
    g_RetryDelayMs = std::min(g_RetryDelayMs * 2, RETRY_MAX_MS);
  }
  if (g_ScanRefusedForCoex.load()) {
    // The join scan was refused because a camera session owns the radio. That
    // session may end within seconds, so the retry must not run out to the
    // full backoff cap. This is the single scheduling path, so a disconnect
    // event that arrives after esp_wifi_connect() returns is covered too.
    g_RetryDelayMs = std::min<uint32_t>(g_RetryDelayMs, COEX_RETRY_MS);
  }
}

void stopForReasonLocked(uint8_t reason) {
  g_ConnectRequested = false;
  g_StoppedForReason = true;
  g_LastDisconnectReason = reason;
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

// Restart the station when the wired address that deferred it disappears.
bool serviceWiredDeferral(void) {
  if (!g_DeferredForWired) {
    return false;
  }
  if (wiredNetworkActive()) {
    return false;
  }

  {
    // Re-arm the request in the same critical section that consumes the resume
    // flag. A manual disconnect that lands after this point clears it again and
    // wins; one that landed before cleared the flag, so no request is created.
    const std::lock_guard<std::mutex> lock(g_StateMutex);
    g_DeferredForWired = false;
    if (!g_ResumeAfterWired.exchange(false)) {
      return false;
    }
    g_ConnectRequested = true;
    g_RetryDelayMs = 0;
    g_DirectFailures = 0;
    g_ConnectAttempts = 0;
    g_StoppedForReason = false;
    g_LastDisconnectReason = 0;
    g_ScanAttempted = false;
    g_FastConnectFailed = false;
    g_LastAttemptUsedBssid.store(false);
  }
  notify(NOTIFY_REQUEST);
  return true;
}

bool scanForNetwork(void) {
  if (!joinScanAllowed(Control::getInstance().getState())) {
    g_ScanRefusedForCoex.store(true);
    ESP_LOGW(LOG_TAG, "refusing to scan while a camera session owns the radio");
    return false;
  }
  g_ScanRefusedForCoex.store(false);

  // Read before the SSID: a clear after this point drops the save below.
  const uint32_t generation = apGeneration();
  const std::string ssid = Settings::load<Settings::WIFI_SSID>();
  wifi_scan_config_t config = {};
  // The SSID is known. Asking the driver for hidden results makes it include
  // hidden APs in every join scan, which does not help a saved non-hidden
  // network and adds probe traffic on each cycle.
  config.show_hidden = false;
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
      saveRememberedAccessPoint(record, generation);
      ESP_LOGI(LOG_TAG, "WiFi found saved access point on channel %u",
               static_cast<unsigned>(record.primary));
      return true;
    }
  }

  ESP_LOGW(LOG_TAG, "WiFi scan did not find SSID %s", ssid.c_str());
  return false;
}

bool connectAttempt(void) {
  if (!g_ConnectRequested || !startDriver()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_DriverMutex);
  if (!g_ConnectRequested || !g_DriverInitialized) {
    stopDriverLocked();
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

uint32_t currentRetryDelay(void) {
  const std::lock_guard<std::mutex> lock(g_StateMutex);
  return g_RetryDelayMs;
}

// Sleep out the scheduled backoff. A lifecycle notification can already be
// pending when this starts (a disconnect delivered inside connectAttempt()), so
// the wait must not treat the first wake as the end of the delay: it enforces an
// absolute deadline while still honoring cancellation, a new connect request
// that reset the backoff, and a link that came up.
bool waitForRetry(uint32_t delay) {
  const int64_t deadlineUs = wifiNowUs() + (delay * 1000LL);
  while (g_ConnectRequested && !g_Connected) {
    if (currentRetryDelay() == 0) {
      return true;
    }
    const int64_t remainingUs = deadlineUs - wifiNowUs();
    if (remainingUs <= 0) {
      return true;
    }
    uint32_t notification = 0;
    xTaskNotifyWait(0, UINT32_MAX, &notification,
                    pdMS_TO_TICKS(static_cast<uint32_t>((remainingUs + 999) / 1000)));
  }
  return false;
}

// Wait for one asynchronous join to produce a lifecycle event. Returns true
// when the link came up or the request was cancelled, false when the explicit
// join deadline expired without any event.
bool waitForJoin(uint32_t lifecycle) {
  const int64_t joinStartedUs = wifiNowUs();
#if defined(FURBLE_WIFI_HOST_TEST)
  // The host shim clock can stay fixed while the stub wait returns at once.
  // Bound the loop so a regression fails instead of hanging CI; the bound
  // counts the attempt exactly like the real deadline.
  uint32_t spins = 0;
#endif
  while (g_ConnectRequested && !g_Connected && (g_LifecycleEvent.load() == lifecycle)
         && ((wifiNowUs() - joinStartedUs) < (JOIN_DEADLINE_MS * 1000LL))) {
#if defined(FURBLE_WIFI_HOST_TEST)
    if (++spins > 10000) {
      ESP_LOGW(LOG_TAG, "WiFi join deadline expired");
      recordFailure();
      return false;
    }
#endif
#if defined(FURBLE_ETHERNET)
    if (wiredNetworkActive()) {
      (void)deferForWired();
      break;
    }
#endif
    uint32_t notification = 0;
    xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(WIRED_RECHECK_MS));
  }
  if (!g_ConnectRequested || g_Connected) {
    return true;
  }
  if (g_LifecycleEvent.load() == lifecycle) {
    // No event within the join budget: count one failed attempt so the retry
    // backoff advances.
    ESP_LOGW(LOG_TAG, "WiFi join deadline expired");
    recordFailure();
  }
  return false;
}

// Issue one connect request and wait for its outcome. The lifecycle snapshot is
// taken before the request, so a disconnect delivered inside connectAttempt()
// ends the wait immediately instead of becoming the baseline for a new
// deadline and a second failure count.
bool connectAndWaitForJoin(void) {
  const uint32_t lifecycle = g_LifecycleEvent.load();
  if (!connectAttempt()) {
    recordFailure();
    return false;
  }
  return waitForJoin(lifecycle);
}

// A connected station must hand the network back when a wired address appears.
// Extracted so the host suite can drive the transition without a task.
bool serviceConnectedWired(void) {
  if (!g_Connected.load() || !wiredNetworkActive()) {
    return false;
  }
  return deferForWired();
}

/*
 * NVS work the event handler hands to the worker. The handler runs on the
 * default event loop task, whose stack is CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE
 * (2,304 bytes then, 3,072 since plan 314 PR 4c); an NVS open, write or the
 * NTP setting read there reached 2,864 bytes in tools/stack_budget.py, before
 * panic paths counted. The worker has 4 KiB. A save and an
 * erase cancel each other, so only the newest outcome is persisted. Guarded by
 * g_StateMutex.
 */
wifi_ap_record_t g_PendingAp = {};
bool g_PendingApSave = false;
bool g_PendingApErase = false;
bool g_PendingNtp = false;

/** Drop a queued save or erase and end the generation. Caller holds g_ApStorageMutex. */
void cancelPendingApStorage(void) {
  std::lock_guard<std::mutex> lock(g_StateMutex);
  g_ApGeneration++;
  g_PendingApSave = false;
  g_PendingApErase = false;
}

/** Run the storage work the event handler deferred. Worker task only. */
void serviceEventStorage(void) {
  bool ntp = false;
  {
    const std::lock_guard<std::mutex> storage(g_ApStorageMutex);
    wifi_ap_record_t record = {};
    bool save = false;
    bool erase = false;
    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      save = g_PendingApSave;
      erase = g_PendingApErase;
      ntp = g_PendingNtp;
      record = g_PendingAp;
      g_PendingApSave = false;
      g_PendingApErase = false;
      g_PendingNtp = false;
    }
    if (erase) {
      eraseStoredAccessPoint();
    }
    if (save) {
      persistAccessPoint(record);
    }
  }
  if (ntp && Settings::load<Settings::NTP>()) {
    startNtp();
  }
}

void wifiTask(void *) {
  while (true) {
    uint32_t notification = 0;
    // Every wait below returns here, so deferred event storage runs before
    // the next one.
    serviceEventStorage();

    if (g_DeferredForWired.load()) {
      xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(WIRED_RECHECK_MS));
      (void)serviceWiredDeferral();
      continue;
    }

#if defined(FURBLE_ETHERNET)
    if (g_Connected.load()) {
      // A connected station used to park here forever, so a cable plugged in
      // after the join left Wi-Fi running on a wired node. Keep polling the
      // wire and hand the network back when it appears.
      xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(WIRED_RECHECK_MS));
      (void)serviceConnectedWired();
      // A terminal disconnect can land during this wait. Without the cleanup
      // the next iteration parks forever on a cleared request and leaves the
      // driver running.
      serviceTerminalStop();
      continue;
    }
#endif

#if defined(FURBLE_ETHERNET)
    // A retryable disconnect clears g_Connected and notifies while the
    // connected-state wait is active, consuming that notification. Bound the
    // idle wait whenever a connection is still requested so the retry loop is
    // re-entered instead of parking until the next external event.
    const TickType_t idleWait =
        g_ConnectRequested ? pdMS_TO_TICKS(WIRED_RECHECK_MS) : portMAX_DELAY;
#else
    const TickType_t idleWait = portMAX_DELAY;
#endif
    xTaskNotifyWait(0, UINT32_MAX, &notification, idleWait);

    while (g_ConnectRequested && !g_Connected) {
      // A failed direct join queues its erase here; persist it before retrying.
      serviceEventStorage();
#if defined(FURBLE_ETHERNET)
      // Ethernet can obtain an address while the station is still associating
      // or backing off. The inner loop never reached the connected-state check,
      // so it kept scanning and connecting on a node that already had a wire.
      if (wiredNetworkActive()) {
        (void)deferForWired();
        break;
      }
#endif
      const uint32_t delay = takeRetryDelay();
      if ((delay > 0) && !waitForRetry(delay)) {
        break;
      }

      bool scan = false;
      {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        scan = WiFiPolicy::shouldScan(g_DirectFailures, g_ScanAttempted, g_FastConnectFailed);
        if (scan) {
          g_ScanAttempted = true;
        }
      }

      if (scan) {
        (void)scanForNetwork();
      }

      // Wait for a lifecycle event instead of issuing another connect request
      // every five seconds. Slow association or DHCP can outlive the
      // cancellation poll; a repeated esp_wifi_connect() would then inflate the
      // failure counters.
      if (connectAndWaitForJoin()) {
        break;
      }
      continue;
    }

    serviceTerminalStop();
  }
}

void eventHandler(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  if ((eventBase == WIFI_EVENT) && (eventId == WIFI_EVENT_STA_CONNECTED)) {
    BootTimeline::mark(BootTimeline::WIFI_ASSOC);
    return;
  }

  if ((eventBase == WIFI_EVENT) && (eventId == WIFI_EVENT_STA_DISCONNECTED)) {
    uint8_t reason = 0;
    if (eventData != nullptr) {
      reason = static_cast<const wifi_event_sta_disconnected_t *>(eventData)->reason;
    }

    const DisconnectClass cls = classifyDisconnect(reason);
    const bool wasConnected = g_Connected.load();
    bool retry = false;
    bool stopped = false;
    bool invalidateRemembered = false;
    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_Connected = false;
      g_GotIp = false;
      g_Ip.clear();
      g_Rssi = 0;
      g_LastDisconnectReason = reason;
      g_PendingNtp = false;

      // A direct join that never reached an IP may be pointing at a BSSID that
      // moved or was replaced. Drop it and scan once before the next attempt.
      // A disconnect after a live session keeps the remembered AP for a fast
      // reconnect.
      if (!wasConnected && g_LastAttemptUsedBssid.load() && g_BssidSet) {
        g_Bssid = {};
        g_BssidSet = false;
        g_Channel = 0;
        g_AuthMode = WiFiPolicy::AUTH_UNKNOWN;
        g_AuthSet = false;
        g_FastConnectFailed = true;
        g_LastAttemptUsedBssid.store(false);
        invalidateRemembered = true;
        g_PendingApErase = true;
        g_PendingApSave = false;
      }

      const uint32_t nextAttempt = g_ConnectAttempts + 1;
      const bool authFailure = cls == DisconnectClass::AUTH;
      const bool noApExhausted =
          (cls == DisconnectClass::NO_AP) && (nextAttempt >= NO_AP_MAX_ATTEMPTS);
      const bool transientExhausted =
          (cls == DisconnectClass::TRANSIENT) && (nextAttempt >= TRANSIENT_MAX_ATTEMPTS);
      if (authFailure || noApExhausted || transientExhausted) {
        stopForReasonLocked(reason);
        stopped = true;
      }
      retry = g_ConnectRequested;
    }
    if (invalidateRemembered) {
      ESP_LOGW(LOG_TAG, "WiFi direct join failed, forgetting the remembered BSSID");
    }
    g_LifecycleEvent.fetch_add(1, std::memory_order_relaxed);
    stopNtp();
    // Count every disconnect. Only a retryable class schedules a backoff, and
    // the last attempt still updates the reported delay before it stops.
    recordFailure(cls != DisconnectClass::AUTH);
    if (retry) {
      ESP_LOGW(LOG_TAG, "WiFi disconnected (reason %u), retrying with backoff", reason);
    } else if (stopped) {
      ESP_LOGW(LOG_TAG,
               "WiFi stopped after disconnect reason %u; fix the setting and run 'wifi connect'",
               reason);
    }
    notify(NOTIFY_EVENT);
    return;
  }

  if ((eventBase == IP_EVENT) && (eventId == IP_EVENT_STA_GOT_IP)) {
    uint32_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      if (!g_ConnectRequested) {
        return;
      }
      generation = g_ApGeneration;
      g_Connected = true;
      g_GotIp = true;
      g_DirectFailures = 0;
      g_ConnectAttempts = 0;
      g_RetryDelayMs = 0;
      g_StoppedForReason = false;
      g_LastDisconnectReason = 0;
      g_ScanAttempted = false;
      g_FastConnectFailed = false;
      g_LastAttemptUsedBssid.store(false);
    }
    BootTimeline::mark(BootTimeline::WIFI_IP);

    wifi_ap_record_t record = {};
    if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
      rememberAccessPoint(record, generation);
      std::lock_guard<std::mutex> lock(g_StateMutex);
      // A clear after this handler read the generation already cancelled the
      // save; do not queue it again.
      if (generation == g_ApGeneration) {
        g_PendingAp = record;
        g_PendingApSave = true;
        g_PendingApErase = false;
      }
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
      if (!g_ConnectRequested) {
        g_Connected = false;
        g_GotIp = false;
        g_Ip.clear();
        g_Rssi = 0;
        return;
      }
      g_Ip = ip;
    }
    g_LifecycleEvent.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(LOG_TAG, "WiFi connected, ip=%s", ip);
    {
      // The worker reads the NTP setting and starts SNTP; see
      // serviceEventStorage().
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_PendingNtp = true;
    }
    notify(NOTIFY_EVENT);
  }
}

}  // namespace

void WiFi::init(void) {
  // Do not create the netif, handlers, or worker when the station is off. A
  // disabled Wi-Fi setting costs a 4 KB task, a netif, and an NVS read at boot
  // for a radio the user does not use. connect() starts the module lazily.
  if (!Settings::load<Settings::WIFI>() || Settings::load<Settings::WIFI_SSID>().empty()) {
    return;
  }
  (void)WiFi::connect();
}

void WiFi::ensureStarted(void) {
  const std::lock_guard<std::mutex> initLock(g_InitMutex);
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
}

bool WiFi::connect(void) {
  if (!Settings::load<Settings::WIFI>() || Settings::load<Settings::WIFI_SSID>().empty()) {
    return false;
  }
  ensureStarted();
  if (!g_Initialized) {
    return false;
  }

  if (wiredNetworkActive()) {
    // A wired node already has an address. Starting the station would add a
    // second radio and a second DHCP client for nothing. Keep the request
    // pending and let the worker restart it when the wire drops. A station that
    // is already up must be stopped here: setting the deferral flag alone would
    // park the worker in its deferred branch with Wi-Fi still running.
    if (g_Connected.load() || g_ConnectRequested) {
      WiFi::disconnect();
    }
    const std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ResumeAfterWired.store(true);
    g_DeferredForWired = true;
    g_ConnectRequested = false;
    notify(NOTIFY_EVENT);
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_DeferredForWired = false;
    g_ConnectRequested = true;
    g_RetryDelayMs = 0;
    g_DirectFailures = 0;
    g_ConnectAttempts = 0;
    g_StoppedForReason = false;
    g_LastDisconnectReason = 0;
    g_ScanAttempted = false;
    g_FastConnectFailed = false;
    g_LastAttemptUsedBssid.store(false);
  }
  BootTimeline::mark(BootTimeline::WIFI_STARTED);
  notify(NOTIFY_REQUEST);
  return true;
}

void WiFi::disconnect(void) {
  {
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ConnectRequested = false;
    g_DeferredForWired = false;
    // An explicit disconnect or disable cancels the wired takeover resume.
    g_ResumeAfterWired = false;
    g_Connected = false;
    g_GotIp = false;
    g_Ip.clear();
    g_Rssi = 0;
  }
  stopNtp();
  notify(NOTIFY_STOP);

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
    status.retry_ms = g_RetryDelayMs;
    status.attempts = g_ConnectAttempts;
    status.disconnect_reason = g_LastDisconnectReason;
    status.stopped = g_StoppedForReason;
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

#if defined(FURBLE_WIFI_HOST_TEST)
bool WiFi::hostServiceWired(void) {
  return serviceWiredDeferral();
}

bool WiFi::hostDeferredForWired(void) {
  return g_DeferredForWired.load();
}

bool WiFi::hostResumeAfterWired(void) {
  return g_ResumeAfterWired.load();
}

void WiFi::hostServiceTerminalStop(void) {
  serviceTerminalStop();
}

void WiFi::hostServiceEventStorage(void) {
  serviceEventStorage();
}

bool WiFi::hostConnectAttempt(void) {
  return connectAttempt();
}

bool WiFi::hostConnectAndWaitForJoin(void) {
  return connectAndWaitForJoin();
}

uint32_t WiFi::hostLifecycleEvent(void) {
  return g_LifecycleEvent.load();
}

uint32_t WiFi::hostWaitForRetry(void) {
  const uint32_t delay = takeRetryDelay();
  if (delay == 0) {
    return 0;
  }
  const int64_t startedUs = wifiNowUs();
  (void)waitForRetry(delay);
  return static_cast<uint32_t>((wifiNowUs() - startedUs) / 1000);
}

bool WiFi::hostServiceConnectedWired(void) {
  return serviceConnectedWired();
}

uint32_t WiFi::hostCoexRetryCap(void) {
  // Drive the common scheduling path with the refusal flag set and return the
  // clamped delay. The caller restores the module state.
  const bool previousRefusal = g_ScanRefusedForCoex.exchange(true);
  for (int i = 0; i < 10; i++) {
    recordFailure();
  }
  const uint32_t delay = [&]() {
    const std::lock_guard<std::mutex> lock(g_StateMutex);
    return g_RetryDelayMs;
  }();
  g_ScanRefusedForCoex.store(previousRefusal);
  return delay;
}

bool WiFi::hostJoinScanAllowed(uint8_t state) {
  return joinScanAllowed(static_cast<Control::state_t>(state));
}

bool WiFi::hostScanForNetwork(void) {
  return scanForNetwork();
}
#endif

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
