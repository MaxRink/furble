#ifndef FURBLE_WEBUI_H
#define FURBLE_WEBUI_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

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

  /** Re-read the opt-in setting and wake the supervisor. */
  void reloadSetting(void);
  bool isRunning(void) const { return m_Running.load(); }
#if defined(FURBLE_WEBUI_HOST_TEST)
  bool hostStartServer(const std::string &certificate, const std::string &privateKey);
  void hostStopServer(void);
  void hostServiceTimerEvents(void);
#endif

 private:
  WebUI() = default;

  static constexpr uint32_t TASK_PERIOD_MS = 500;
  static constexpr uint32_t MAX_HOLD_MS = 60 * 1000;
  static constexpr uint32_t RELEASE_RETRY_MS = 20;
  static constexpr size_t MAX_BODY_BYTES = 1024;
  static constexpr uint8_t HELD_SHUTTER = 1U << 0;
  static constexpr uint8_t HELD_FOCUS = 1U << 1;
  static constexpr uint8_t TIMER_HOLD = 1U << 0;
  static constexpr uint8_t TIMER_RELEASE_RETRY = 1U << 1;

  static void taskEntry(void *param);
  void task(void);
  void serviceTimerEvents(void);
  bool networkReady(void) const;
  bool credentialsReady(void);
  bool startServer(void);
  void stopServer(void);

  bool loadOrCreateIdentity(void);
  bool createIdentity(void);
  void logCertificateFingerprint(void) const;

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

  bool queuePress(Control::cmd_t command, uint8_t heldBit);
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
  httpd_handle_t m_Server = nullptr;
  esp_timer_handle_t m_HoldTimer = nullptr;
  esp_timer_handle_t m_ReleaseTimer = nullptr;
  std::atomic<bool> m_Running = false;
  bool m_Stopping = true;
  std::atomic<uint8_t> m_TimerEvents = 0;
  std::atomic<int64_t> m_HoldDeadlineUs = 0;
  bool m_Reload = false;
  uint8_t m_Held = 0;
  uint8_t m_ReleasePending = 0;
  uint32_t m_CommandSession = 0;
  std::string m_Certificate;
  std::string m_PrivateKey;

  std::mutex m_AuthMutex;
  uint8_t m_AuthFailures = 0;
  uint64_t m_AuthBlockedUntilMs = 0;
};

}  // namespace Furble

#endif
