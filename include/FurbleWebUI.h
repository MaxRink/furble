#ifndef FURBLE_WEBUI_H
#define FURBLE_WEBUI_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include <esp_http_server.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleControl.h"

namespace Furble {

/**
 * Lightweight HTTP server exposing a browser WebUI and a small REST API.
 *
 * Gated behind the WEB_UI setting, which defaults off. When enabled the
 * supervisor task waits for a WiFi station address and then starts
 * esp_http_server. Every request handler runs on the esp_http_server task, so
 * the BLE, UI and control tasks are never blocked. Shutter and settings
 * actions go through the same thread-safe paths the console and companion use;
 * the Control mutex is never held across a handler.
 */
class WebUI {
 public:
  static WebUI &getInstance(void);

  WebUI(WebUI const &) = delete;
  WebUI(WebUI &&) = delete;
  WebUI &operator=(WebUI const &) = delete;
  WebUI &operator=(WebUI &&) = delete;

  /** Start the supervisor task when WEB_UI is enabled. */
  static void init(void);

  /** Re-read WEB_UI and start or stop the server to match. */
  void reloadSetting(void);

  bool isRunning(void) const { return m_Running.load(); }

 private:
  WebUI() = default;

  static constexpr uint16_t HTTP_PORT = 80;
  static constexpr uint32_t TASK_PERIOD_MS = 500;
  static constexpr uint32_t MAX_HOLD_MS = 60 * 1000;
  static constexpr size_t MAX_BODY_BYTES = 1024;

  static void taskEntry(void *param);
  void task(void);

  bool networkReady(void) const;
  bool startServer(void);
  void stopServer(void);

  static esp_err_t handleRoot(httpd_req_t *req);
  static esp_err_t handleStatus(httpd_req_t *req);
  static esp_err_t handleCameras(httpd_req_t *req);
  static esp_err_t handleShutter(httpd_req_t *req);
  static esp_err_t handleSettingsGet(httpd_req_t *req);
  static esp_err_t handleSettingsPost(httpd_req_t *req);

  std::string buildStatusJSON(void) const;
  std::string buildCamerasJSON(void) const;
  std::string buildSettingsJSON(void) const;
  bool applyShutter(const std::string &body, std::string &error);
  bool applySettings(const std::string &body, std::string &error);

  bool sendCommand(Control::cmd_t command);
  bool sendHold(uint32_t duration_ms);
  void releaseHold(void);
  static void holdTimerCallback(void *arg);

  static esp_err_t recvBody(httpd_req_t *req, std::string &out);
  static esp_err_t sendJSON(httpd_req_t *req, const std::string &json);
  static esp_err_t sendJSONError(httpd_req_t *req, const char *status, const std::string &message);

  mutable std::mutex m_Mutex;
  TaskHandle_t m_Task = nullptr;
  httpd_handle_t m_Server = nullptr;
  esp_timer_handle_t m_HoldTimer = nullptr;

  std::atomic<bool> m_Running = false;
  bool m_Reload = false;
  bool m_HoldActive = false;
  mutable uint32_t m_LastNetworkLogMs = 0;
};

}  // namespace Furble

#endif
