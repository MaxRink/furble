// Precise allocator-instrumented TLS/socket boundary for the production WebUI.
//
// FurbleWebUI.cpp is compiled unchanged. The esp_https_server double charges
// the exact TLS context, record-buffer and socket sizes and can refuse any one
// allocation, so the production start gate, failure branch and stop/reopen
// ordering are exercised without a physical TLS stack.

#include <iostream>
#include <map>
#include <string>

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleWebUI.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_timer.h"

namespace {

int failures = 0;

const std::map<std::string, std::string> validHeaders = {
    {"Authorization", "Basic ZnVyYmxlOmNvcnJlY3QgaG9yc2U="},
    {"Content-Type",  "application/json"                  },
    {"Host",          "furble.test"                       },
    {"Origin",        "https://furble.test"               },
};

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "TLS FAIL: " << message << '\n';
    failures++;
  }
}

}  // namespace

int main() {
  using Furble::Control;
  auto &control = Control::getInstance();
  auto &webui = Furble::WebUI::getInstance();
  control.reset();
  host_webui_http::reset();
  host_webui_timer::reset();
  host_webui_heap::reset();
  Furble::Settings::password = "correct horse";
  Furble::Settings::passwordLoaded = true;

  check(webui.hostCredentialsReady(), "the test identity was not prepared");
  check(webui.hostStartServer("test certificate", "test key"), "the WebUI did not start");

  const size_t expectedBytes = host_webui_tls::TLS_CONTEXT_BYTES
                               + host_webui_tls::IN_BUFFER_BYTES
                               + host_webui_tls::OUT_BUFFER_BYTES
                               + (2 * host_webui_tls::SOCKET_BYTES);
  check(host_webui_tls::handshakeCount() == 1, "the TLS handshake was not started once");
  check(host_webui_tls::liveBytes() == expectedBytes,
        "the TLS/socket allocation ledger does not match the production config");
  check(host_webui_tls::socketCount() == 2, "the production socket count did not reach the layer");
  check(host_webui_tls::lastHandshakeTimeoutMs() == 5000,
        "the production TLS handshake timeout changed");
  std::cout << "tls.handshake handshakes=1 bytes=" << expectedBytes
            << " sockets=2 timeout_ms=5000\n";

  // A header over the production 256-byte scratch bound is refused before any
  // control command is reached.
  auto longHeader = validHeaders;
  longHeader["Authorization"] = "Basic " + std::string(300, 'A');
  const auto headerResult =
      host_webui_http::invoke(HTTP_POST, "/api/shutter", longHeader, R"({"action":"press"})");
  check(headerResult.responseStatus == "401 Unauthorized",
        "an oversized authorization header was not refused");
  check(control.commands.empty(), "an oversized header reached Control");
  std::cout << "tls.header oversized=1 status=401 commands=0\n";

  // A body over MAX_BODY_BYTES is refused by the production handler.
  const std::string oversizedBody = R"({"action":"press","padding":")" + std::string(1100, 'x')
                                    + R"("})";
  const auto bodyResult =
      host_webui_http::invoke(HTTP_POST, "/api/shutter", validHeaders, oversizedBody);
  check(bodyResult.responseStatus == "413 Payload Too Large",
        "an oversized request body was not refused");
  check(control.commands.empty(), "an oversized body reached Control");
  std::cout << "tls.body oversized=1 status=413 commands=0\n";

  // Close and reopen must release every allocation and handshake again.
  webui.hostStopServer();
  check(host_webui_tls::liveBytes() == 0, "the TLS/socket allocations survived close");
  check(host_webui_tls::closeCount() == 1, "the close was not recorded once");
  check(webui.hostStartServer("test certificate", "test key"), "the WebUI did not reopen");
  check(host_webui_tls::handshakeCount() == 2, "the reopen did not handshake again");
  check(host_webui_tls::liveBytes() == expectedBytes, "the reopen did not reacquire every allocation");
  check(host_webui_tls::socketCount() == 2, "the reopen did not restore both sockets");
  std::cout << "tls.reopen handshakes=2 bytes=" << host_webui_tls::liveBytes() << '\n';

  // A precise allocation failure must fail the start and keep the persisted
  // identity for the next attempt. The production branch also logs the exact
  // ESP_ERR_HTTPD_ALLOC_MEM name.
  webui.hostStopServer();
  const auto identity = Preferences::values;
  host_webui_tls::setFailAt(2);
  check(!webui.hostStartServer("test certificate", "test key"),
        "an allocation failure did not fail the start");
  check(host_webui_tls::liveBytes() == host_webui_tls::TLS_CONTEXT_BYTES,
        "the failed start did not release only the allocations it took");
  check(Preferences::values == identity, "the allocation failure rewrote the identity");
  std::cout << "tls.alloc_failure failed=1 retained_identity=1\n";
  host_webui_tls::reset();
  webui.hostResetIdentity();
  check(webui.hostCredentialsReady(), "the identity was lost after the allocation failure");
  check(webui.hostStartServer("test certificate", "test key"),
        "the WebUI did not recover after the allocation failure");
  check(host_webui_tls::handshakeCount() == 1, "the recovered start did not handshake");
  webui.hostStopServer();
  check(host_webui_tls::liveBytes() == 0, "the recovered close leaked allocations");
  std::cout << "tls.alloc_recovery started=1 live_bytes=0\n";

  if (failures != 0) {
    return 1;
  }
  std::cout << "webui_tls_socket_test: PASS\n";
  return 0;
}
