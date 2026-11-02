#ifndef FURBLE_HOST_WEBUI_HTTPS_SERVER_H
#define FURBLE_HOST_WEBUI_HTTPS_SERVER_H
#include <cstdint>
#include "esp_http_server.h"
struct httpd_ssl_config_t {
  struct {
    int stack_size = 0;
    int max_uri_handlers = 0;
    int recv_wait_timeout = 0;
    int send_wait_timeout = 0;
  } httpd;
  int tls_handshake_timeout_ms = 0;
  const uint8_t *servercert = nullptr;
  size_t servercert_len = 0;
  const uint8_t *prvtkey_pem = nullptr;
  size_t prvtkey_len = 0;
};
inline httpd_ssl_config_t HTTPD_SSL_CONFIG_DEFAULT() {
  return {};
}
extern "C" esp_err_t httpd_ssl_start(httpd_handle_t *server, const httpd_ssl_config_t *config);
extern "C" esp_err_t httpd_ssl_stop(httpd_handle_t server);
#endif
