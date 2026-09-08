#include "esp_https_server.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

struct host_webui_server_t {
  std::vector<httpd_uri_t> routes;
};
struct host_webui_timer_t {
  esp_timer_cb_t callback = nullptr;
  void *arg = nullptr;
  uint64_t deadline = 0;
  bool active = false;
};

namespace {
host_webui_server_t *g_Server = nullptr;
std::vector<std::unique_ptr<host_webui_timer_t>> g_Timers;
uint64_t g_Now = 0;
uint64_t g_StartAdvance = 0;
std::vector<std::pair<esp_timer_cb_t, void *>> g_QueuedCallbacks;
std::function<void()> g_StopHook;
}

extern "C" size_t httpd_req_get_hdr_value_len(httpd_req_t *request, const char *name) {
  const auto found = request->headers.find(name);
  return found == request->headers.end() ? 0 : found->second.size();
}
extern "C" esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *request, const char *name,
                                                   char *value, size_t length) {
  const auto found = request->headers.find(name);
  if ((found == request->headers.end()) || (length <= found->second.size())) return ESP_FAIL;
  std::memcpy(value, found->second.c_str(), found->second.size() + 1);
  return ESP_OK;
}
extern "C" int httpd_req_recv(httpd_req_t *request, char *buffer, size_t length) {
  const size_t remaining = request->body.size() - request->bodyOffset;
  const size_t count = std::min(length, remaining);
  if (count == 0) return 0;
  std::memcpy(buffer, request->body.data() + request->bodyOffset, count);
  request->bodyOffset += count;
  return static_cast<int>(count);
}
extern "C" esp_err_t httpd_resp_set_status(httpd_req_t *request, const char *status) {
  request->responseStatus = status;
  return ESP_OK;
}
extern "C" esp_err_t httpd_resp_set_hdr(httpd_req_t *request, const char *name, const char *value) {
  request->responseHeaders[name] = value;
  return ESP_OK;
}
extern "C" esp_err_t httpd_resp_set_type(httpd_req_t *request, const char *type) {
  request->responseType = type;
  return ESP_OK;
}
extern "C" esp_err_t httpd_resp_send(httpd_req_t *request, const char *body, ssize_t length) {
  request->responseBody.assign(body, length == HTTPD_RESP_USE_STRLEN ? std::strlen(body)
                                                                     : static_cast<size_t>(length));
  return ESP_OK;
}
extern "C" esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route) {
  server->routes.push_back(*route);
  return ESP_OK;
}
extern "C" esp_err_t httpd_ssl_start(httpd_handle_t *server, const httpd_ssl_config_t *) {
  *server = new host_webui_server_t;
  g_Server = *server;
  return ESP_OK;
}
extern "C" esp_err_t httpd_ssl_stop(httpd_handle_t server) {
  if (g_StopHook) g_StopHook();
  if (g_Server == server) g_Server = nullptr;
  delete server;
  return ESP_OK;
}

namespace host_webui_http {
void reset() { g_StopHook = {}; }
httpd_req_t invoke(httpd_method_t method, const std::string &uri,
                   const std::map<std::string, std::string> &headers, const std::string &body) {
  httpd_req_t request;
  request.headers = headers;
  request.body = body;
  request.content_len = body.size();
  if (g_Server == nullptr) {
    request.responseStatus = "503 No Server";
    return request;
  }
  const auto found = std::find_if(g_Server->routes.begin(), g_Server->routes.end(),
                                  [&](const auto &route) {
                                    return (route.method == method) && (uri == route.uri);
                                  });
  if (found == g_Server->routes.end()) {
    request.responseStatus = "404 Not Found";
    return request;
  }
  request.user_ctx = found->user_ctx;
  found->handler(&request);
  return request;
}
void setStopHook(std::function<void()> hook) { g_StopHook = std::move(hook); }
}  // namespace host_webui_http

extern "C" int64_t esp_timer_get_time() { return static_cast<int64_t>(g_Now); }
extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                                       esp_timer_handle_t *handle) {
  auto timer = std::make_unique<host_webui_timer_t>();
  timer->callback = args->callback;
  timer->arg = args->arg;
  *handle = timer.get();
  g_Timers.push_back(std::move(timer));
  return ESP_OK;
}
extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout) {
  timer->deadline = g_Now + timeout;
  timer->active = true;
  g_Now += g_StartAdvance;
  g_StartAdvance = 0;
  return ESP_OK;
}
extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  timer->active = false;
  return ESP_OK;
}
extern "C" bool esp_timer_is_active(esp_timer_handle_t timer) { return timer->active; }
namespace host_webui_timer {
void reset() {
  g_Now = 0;
  g_StartAdvance = 0;
  g_QueuedCallbacks.clear();
  for (auto &timer : g_Timers) timer->active = false;
}
void elapseAndQueue(uint64_t microseconds) {
  g_Now += microseconds;
  for (auto &timer : g_Timers) {
    if (timer->active && (timer->deadline <= g_Now)) {
      timer->active = false;
      g_QueuedCallbacks.emplace_back(timer->callback, timer->arg);
    }
  }
}
void dispatchQueued() {
  const auto callbacks = std::move(g_QueuedCallbacks);
  g_QueuedCallbacks.clear();
  for (const auto &[callback, arg] : callbacks) callback(arg);
}
void advance(uint64_t microseconds) {
  elapseAndQueue(microseconds);
  dispatchQueued();
}
void setStartAdvance(uint64_t microseconds) { g_StartAdvance = microseconds; }
}  // namespace host_webui_timer

extern "C" BaseType_t xTaskCreate(void (*)(void *), const char *, uint32_t, void *, UBaseType_t,
                                   TaskHandle_t *handle) {
  *handle = reinterpret_cast<void *>(1);
  return pdPASS;
}
extern "C" BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdTRUE; }
extern "C" uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
