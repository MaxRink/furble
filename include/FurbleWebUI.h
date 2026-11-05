#ifndef FURBLE_WEBUI_H
#define FURBLE_WEBUI_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <esp_http_server.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <FurbleControl.h>

namespace Furble {

/** Authenticated HTTPS control and settings surface for network hub builds. */
class WebUI {
 public:
  static WebUI &getInstance(void);
  static void init(void);

  WebUI(WebUI const &) = delete;
  WebUI(WebUI &&) = delete;
  WebUI &operator=(WebUI const &) = delete;
  WebUI &operator=(WebUI &&) = delete;

  /**
   * Load or create the TLS identity before the radios and display consume the
   * internal heap.
   *
   * ECDSA/X.509 generation needs a multi-kilobyte stack and writes NVS. On the
   * StickS3 neither fits after WiFi and the LVGL draw buffers are up, so the
   * identity is prepared here while NVS writes are safe. The work runs on the
   * dedicated 12 KiB identity task rather than the main task, which lets the
   * main stack stay at 8 KiB. The main task blocks until the identity task
   * finishes so the heap ordering does not change. The supervisor itself never
   * generates an identity on its own stack.
   */
  static void initEarly(void);

  /** Re-read the opt-in setting and wake the supervisor. */
  void reloadSetting(void);
  bool isRunning(void) const { return m_Running.load(); }
#if defined(FURBLE_WEBUI_HOST_TEST)
  bool hostStartServer(const std::string &certificate, const std::string &privateKey);
  void hostStopServer(void);
  /** Run the credential gate without starting the server. */
  bool hostCredentialsReady(void);
  /** Forget the cached identity so the next gate reads NVS again. */
  void hostResetIdentity(void);
  /** The cached password half of the credential gate. */
  bool hostPasswordReady(void) const { return m_PasswordReady.load(); }
  void hostServiceTimerEvents(void);
#endif

 private:
  WebUI() = default;

  static constexpr uint32_t TASK_PERIOD_MS = 500;
  static constexpr uint32_t MAX_HOLD_MS = 60 * 1000;
  static constexpr uint32_t RELEASE_RETRY_MS = 200;
  static constexpr uint32_t MANUAL_PRESS_LEASE_MS = 30 * 1000;
  static constexpr uint32_t BUDGET_RETRY_MS = 10 * 1000;
  static constexpr uint32_t IDENTITY_STACK_BYTES = 12288;
  static constexpr size_t MAX_BODY_BYTES = 1024;
  static constexpr uint8_t HELD_SHUTTER = 1U << 0;
  static constexpr uint8_t HELD_FOCUS = 1U << 1;
  static constexpr uint8_t TIMER_HOLD = 1U << 0;
  static constexpr uint8_t TIMER_RELEASE_RETRY = 1U << 1;
  // This is the total socket budget: ESP-IDF reserves three sockets per HTTP
  // server for its listener and accept bookkeeping, so five total sockets leave
  // two client slots. That is enough for one browser plus a retry/health probe.
  static constexpr uint8_t HTTPD_MAX_SOCKETS = 5;
  static constexpr size_t HTTPD_MAX_REQ_HDR_LEN = 1024;
  static constexpr size_t HTTPD_MAX_RESP_HEADERS = 7;
  static constexpr size_t HTTPD_MAX_URI_HANDLERS = 7;

  static void taskEntry(void *param);
  static void identityTaskEntry(void *param);
  void task(void);
  void identityTask(void);
  void serviceTimerEvents(void);
  bool networkReady(void) const;
  bool credentialsReady(void) const;
  bool startServer(void);
  void stopServer(void);
  bool serverMemoryAvailable(void) const;
  void reportHttpdStack(void);

  /** Caller must hold m_IdentityMutex. */
  bool prepareIdentityLocked(void);
  /** Caller must hold m_IdentityMutex. */
  bool createIdentity(void);
  /** Caller must hold m_IdentityMutex. */
  void logCertificateFingerprint(void);

  static esp_err_t handleRoot(httpd_req_t *req);
  static esp_err_t handleStatus(httpd_req_t *req);
  static esp_err_t handleCamerasGet(httpd_req_t *req);
  static esp_err_t handleCamerasPost(httpd_req_t *req);
  static esp_err_t handleShutter(httpd_req_t *req);
  static esp_err_t handleSettingsGet(httpd_req_t *req);
  static esp_err_t handleSettingsPost(httpd_req_t *req);

  bool authorize(httpd_req_t *req);
  bool verifyBasic(const std::string &header);
  bool requestAllowed(httpd_req_t *req, bool mutation);
  bool applyCamera(const std::string &body, std::string &error);
  bool applyShutter(const std::string &body, std::string &error);
  bool applySettings(const std::string &body, std::string &error);
  std::string buildStatusJSON(void) const;
  std::string buildCamerasJSON(void) const;
  std::string buildSettingsJSON(void) const;

  bool queuePress(Control::cmd_t command, uint8_t heldBit, bool startLease = false);
  bool queueRelease(Control::cmd_t command, uint8_t heldBit);
  bool queueHold(uint32_t durationMs);
  bool syncCommandSession(uint32_t session);
  void releaseAll(void);
  void retryReleases(void);
  static void holdTimerCallback(void *arg);
  static void releaseTimerCallback(void *arg);

  static void settingApplied(uint8_t wireId);
  static esp_err_t recvBody(httpd_req_t *req, std::string &out);
  static esp_err_t sendJSON(httpd_req_t *req, const std::string &json);
  static esp_err_t sendError(httpd_req_t *req, const char *status, const char *message);
  static void setSecurityHeaders(httpd_req_t *req);

  mutable std::mutex m_Mutex;
  std::recursive_mutex m_CommandMutex;
  TaskHandle_t m_Task = nullptr;
  // Set by initEarly() while it waits for the dedicated identity task. The
  // task clears and notifies it before it exits.
  TaskHandle_t m_EarlyIdentityWaiter = nullptr;
  httpd_handle_t m_Server = nullptr;
  esp_timer_handle_t m_HoldTimer = nullptr;
  esp_timer_handle_t m_ReleaseTimer = nullptr;
  std::atomic<bool> m_Running = false;
  bool m_Stopping = true;
  std::atomic<uint8_t> m_TimerEvents = 0;
  std::atomic<int64_t> m_HoldDeadlineUs = 0;
  bool m_Reload = false;
  // The supervisor no longer reads NVS. Setting and password state is cached
  // by reloadSetting() and initEarly(), then only the booleans are polled.
  std::atomic<bool> m_Enabled = false;
  std::atomic<bool> m_PasswordReady = false;
  std::atomic<bool> m_CredentialsReady = false;
  std::atomic<bool> m_IdentityTaskActive = false;
  std::atomic<uint64_t> m_BudgetRetryAfterMs = 0;
  std::atomic<uint32_t> m_HttpdStackLowWater = 0;
  uint8_t m_Held = 0;
  uint8_t m_ReleasePending = 0;
  uint32_t m_CommandSession = 0;
  // Targets that accepted each held press and still owe a release. Addresses
  // survive a reconnect inside the same session, so a target that missed the
  // first release keeps its slot and receives the retry when it is back.
  std::vector<uint64_t> m_HeldShutterTargets;
  std::vector<uint64_t> m_HeldFocusTargets;
  int64_t m_FocusLeaseDeadlineUs = 0;
  bool m_IdentityStartFailed = false;
  std::vector<uint64_t> &heldTargets(uint8_t heldBit);
  void startManualLease(uint8_t heldBit);
  // Identity state is shared by the early boot task, the one-shot generation
  // task and the supervisor. m_IdentityMutex guards all three strings.
  std::mutex m_IdentityMutex;
  std::string m_Certificate;
  std::string m_PrivateKey;
  std::string m_Fingerprint;

  std::mutex m_AuthMutex;
  uint8_t m_AuthFailures = 0;
  uint64_t m_AuthBlockedUntilMs = 0;
};

}  // namespace Furble

#endif
