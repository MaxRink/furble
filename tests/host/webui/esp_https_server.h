#ifndef FURBLE_HOST_WEBUI_HTTPS_SERVER_H
#define FURBLE_HOST_WEBUI_HTTPS_SERVER_H
#include <cstdint>
#include "esp_http_server.h"
struct httpd_ssl_config_t {
  struct {
    int stack_size = 0;
    int max_uri_handlers = 0;
    int max_open_sockets = 0;
    bool lru_purge_enable = false;
    int max_req_hdr_len = 0;
    int max_resp_headers = 0;
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

namespace host_webui_tls {
/** Exact TLS/socket allocation sizes charged by the socket layer double. */
constexpr size_t TLS_CONTEXT_BYTES = 16 * 1024;
constexpr size_t IN_BUFFER_BYTES = 4 * 1024;
constexpr size_t OUT_BUFFER_BYTES = 4 * 1024;
constexpr size_t SOCKET_BYTES = 2 * 1024;

void reset();
/** Fail the n-th allocation (1-based); zero disables the injection. */
void setFailAt(size_t index);
size_t handshakeCount();
size_t closeCount();
size_t liveBytes();
size_t socketCount();
int lastHandshakeTimeoutMs();
}  // namespace host_webui_tls
#endif
