#ifndef FURBLE_HOST_WEBUI_HTTP_SERVER_H
#define FURBLE_HOST_WEBUI_HTTP_SERVER_H

#include <sys/types.h>
#include <cstddef>
#include <functional>
#include <map>
#include <string>

#include "esp_err.h"

enum httpd_method_t { HTTP_GET, HTTP_POST };
struct httpd_req_t;
using httpd_uri_func_t = esp_err_t (*)(httpd_req_t *);
struct httpd_uri_t {
  const char *uri;
  httpd_method_t method;
  httpd_uri_func_t handler;
  void *user_ctx;
};
struct httpd_req_t {
  size_t content_len = 0;
  void *user_ctx = nullptr;
  std::map<std::string, std::string> headers;
  std::string body;
  size_t bodyOffset = 0;
  std::string responseStatus = "200 OK";
  std::string responseType;
  std::map<std::string, std::string> responseHeaders;
  std::string responseBody;
};
struct host_webui_server_t;
using httpd_handle_t = host_webui_server_t *;

constexpr int HTTPD_SOCK_ERR_TIMEOUT = -2;
constexpr ssize_t HTTPD_RESP_USE_STRLEN = -1;

extern "C" {
size_t httpd_req_get_hdr_value_len(httpd_req_t *request, const char *name);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *request,
                                      const char *name,
                                      char *value,
                                      size_t length);
int httpd_req_recv(httpd_req_t *request, char *buffer, size_t length);
esp_err_t httpd_resp_set_status(httpd_req_t *request, const char *status);
esp_err_t httpd_resp_set_hdr(httpd_req_t *request, const char *name, const char *value);
esp_err_t httpd_resp_set_type(httpd_req_t *request, const char *type);
esp_err_t httpd_resp_send(httpd_req_t *request, const char *body, ssize_t length);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
}

namespace host_webui_http {
void reset();
httpd_req_t invoke(httpd_method_t method,
                   const std::string &uri,
                   const std::map<std::string, std::string> &headers,
                   const std::string &body = {});
void setStopHook(std::function<void()> hook);
}  // namespace host_webui_http

#endif
