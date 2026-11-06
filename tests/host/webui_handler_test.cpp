#include <iostream>
#include <map>
#include <string>

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleWebUI.h"
#include "FurbleWebUIMemory.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
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
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

httpd_req_t shutter(const std::string &body,
                    const std::map<std::string, std::string> &headers = validHeaders) {
  return host_webui_http::invoke(HTTP_POST, "/api/shutter", headers, body);
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

  // Identity persistence. A read error must not be mistaken for a missing key,
  // and a generated identity that could not be persisted must not stay cached.
  Preferences::reset();
  webui.hostResetIdentity();
  Preferences::readFail = true;
  check(!webui.hostCredentialsReady(), "a TLS identity read error keeps HTTPS closed");
  check(Preferences::values.empty(), "a read error does not overwrite the stored identity");
  Preferences::readFail = false;

  Preferences::writableOpenFail = true;
  check(!webui.hostCredentialsReady(), "a writable NVS open failure keeps HTTPS closed");
  check(Preferences::values.empty(), "an unpersisted identity is not cached");
  Preferences::writableOpenFail = false;

  check(webui.hostCredentialsReady(), "a missing identity is generated and persisted");
  check(Preferences::values.size() == 2, "the certificate and key were persisted");
  webui.hostResetIdentity();
  check(webui.hostCredentialsReady(), "a persisted identity is reused");

  // The password half of the gate is refreshed by reloadSetting(), including
  // when a companion GATT write clears the credential. The WebUI-off branch
  // must not create an identity task or write NVS.
  const auto identityBeforeGate = Preferences::values;
  Preferences::values.clear();
  webui.hostResetIdentity();
  Furble::Settings::webEnabled = false;
  Furble::Settings::password = "";
  webui.reloadSetting();
  check(!webui.hostPasswordReady(), "reloadSetting clears the cached password gate");
  check(Preferences::values.empty(), "the WebUI-off reload does not create an identity");
  Furble::Settings::password = "correct horse";
  webui.reloadSetting();
  check(webui.hostPasswordReady(), "reloadSetting restores the cached password gate");
  check(Preferences::values.empty(), "the WebUI-off reload never generates on the caller task");
  Furble::Settings::webEnabled = true;
  Preferences::values = identityBeforeGate;
  webui.hostResetIdentity();

  // The heap gate must refuse the server before allocating its task, and a
  // failed start due to memory pressure must not throw away the persisted
  // identity. Regeneration would add NVS writes to an already low heap.
  const auto persistedIdentity = Preferences::values;
  host_webui_heap::set(Furble::WebUIMemory::SERVER_INTERNAL_FREE_FLOOR - 1,
                       Furble::WebUIMemory::SERVER_INTERNAL_LARGEST_FLOOR);
  check(!webui.hostStartServer("test certificate", "test key"),
        "the WebUI started below the internal free floor");
  host_webui_heap::set(Furble::WebUIMemory::SERVER_INTERNAL_FREE_FLOOR,
                       Furble::WebUIMemory::SERVER_INTERNAL_LARGEST_FLOOR - 1);
  check(!webui.hostStartServer("test certificate", "test key"),
        "the WebUI started below the largest-block floor");
  host_webui_heap::reset();
  host_webui_http::setStartError(ESP_ERR_NO_MEM);
  check(!webui.hostStartServer("test certificate", "test key"),
        "a simulated out-of-memory start succeeded");
  host_webui_http::setStartError(ESP_OK);
  webui.hostResetIdentity();
  check(webui.hostCredentialsReady(), "an out-of-memory start discarded the identity");
  check(Preferences::values == persistedIdentity,
        "an out-of-memory start rewrote the persisted identity");
  webui.hostResetIdentity();

  check(webui.hostStartServer("test certificate", "test key"), "real WebUI server did not start");
  check(host_webui_http::lastMaxOpenSockets() == Furble::WebUI::maxOpenSockets(),
        "the WebUI start did not carry the production client socket count");

  // Settings serialization and writes. The catalog is populated by the host
  // double so this exercises redaction, typed writes and the password guard.
  const auto settingsGet = host_webui_http::invoke(HTTP_GET, "/api/settings", validHeaders, "");
  check(settingsGet.responseStatus == "200 OK", "settings GET succeeded");
  check(settingsGet.responseBody.find("\"secret\":true") != std::string::npos,
        "the companion password is marked secret");
  check(settingsGet.responseBody.find("\"set\":true") != std::string::npos,
        "the companion password is reported set");
  check(settingsGet.responseBody.find("correct horse") == std::string::npos,
        "the companion password value is redacted");
  check(settingsGet.responseBody.find("\"value\":77") != std::string::npos,
        "a non-secret setting is serialized with its value");

  const auto brightnessPost =
      host_webui_http::invoke(HTTP_POST, "/api/settings", validHeaders, R"({"id":20,"value":42})");
  check(brightnessPost.responseStatus == "200 OK", "a typed setting write succeeds");
  const auto secretPost = host_webui_http::invoke(HTTP_POST, "/api/settings", validHeaders,
                                                  R"({"id":40,"value":"nope"})");
  check(secretPost.responseStatus == "400 Bad Request",
        "the companion password is not writable through the WebUI");
  check(secretPost.responseBody.find("not writable here") != std::string::npos,
        "the password rejection names the reason");

  auto noAuth = validHeaders;
  noAuth.erase("Authorization");
  check(shutter(R"({"action":"press"})", noAuth).responseStatus == "401 Unauthorized",
        "unauthenticated shutter mutation was accepted");
  check(control.commands.empty(), "unauthenticated request reached Control");

  auto wrongAuth = validHeaders;
  wrongAuth["Authorization"] = "Basic ZnVyYmxlOndyb25n";
  check(shutter(R"({"action":"press"})", wrongAuth).responseStatus == "401 Unauthorized",
        "wrong-password shutter mutation was accepted");
  check(control.commands.empty(), "wrong-password request reached Control");

  auto wrongOrigin = validHeaders;
  wrongOrigin["Origin"] = "https://evil.example";
  check(shutter(R"({"action":"press"})", wrongOrigin).responseStatus == "403 Forbidden",
        "cross-origin shutter mutation was accepted");

  auto wrongType = validHeaders;
  wrongType["Content-Type"] = "text/plain";
  check(shutter(R"({"action":"press"})", wrongType).responseStatus == "415 Unsupported Media Type",
        "non-JSON shutter mutation was accepted");

  check(shutter(R"({"action":"hold","ms":1000})").responseStatus == "200 OK",
        "timed hold was rejected");
  check(control.commands.size() == 1 && control.commands.back() == Control::CMD_SHUTTER_PRESS,
        "timed hold did not enqueue press");
  host_webui_timer::elapseAndQueue(1000 * 1000);
  check(shutter(R"({"action":"release"})").responseStatus == "200 OK",
        "manual release was rejected");
  check(shutter(R"({"action":"press"})").responseStatus == "200 OK",
        "plain press after manual release was rejected");
  const size_t beforeStaleExpiry = control.commands.size();
  host_webui_timer::dispatchQueued();
  webui.hostServiceTimerEvents();
  check(control.commands.size() == beforeStaleExpiry,
        "already-dispatched hold callback released the newer plain press");

  control.deliveries.push_back({false, false, false, 0, {}});
  check(shutter(R"({"action":"release"})").responseStatus == "200 OK",
        "failed release was not retained for retry");
  const size_t beforeRejectedHold = control.commands.size();
  check(shutter(R"({"action":"hold","ms":100})").responseStatus == "409 Conflict",
        "new hold was accepted while release remained pending");
  check(control.commands.size() == beforeRejectedHold, "pending-release hold reached Control");
  control.deliveries.push_back({true, true, true, 0, {1}});
  host_webui_timer::advance(200 * 1000);
  webui.hostServiceTimerEvents();
  check(control.commands.back() == Control::CMD_SHUTTER_RELEASE,
        "pending release was not retried by the owner task");

  host_webui_timer::setStartAdvance(500);
  check(shutter(R"({"action":"hold","ms":1})").responseStatus == "200 OK",
        "1 ms hold was rejected");
  host_webui_timer::advance(500);
  webui.hostServiceTimerEvents();
  check(control.commands.back() == Control::CMD_SHUTTER_RELEASE,
        "1 ms hold expiry was lost when timer arming consumed time");

  check(shutter(R"({"action":"press"})").responseStatus == "200 OK",
        "plain press before session replacement was rejected");
  const size_t beforeReplacementPress = control.commands.size();
  control.state = Control::STATE_IDLE;
  control.targets = 0;
  control.connected = 0;
  control.session++;
  control.state = Control::STATE_ACTIVE;
  control.targets = 1;
  control.connected = 1;
  check(shutter(R"({"action":"press"})").responseStatus == "200 OK",
        "plain press after session replacement was rejected");
  check(control.commands.size() == beforeReplacementPress + 1
            && control.commands.back() == Control::CMD_SHUTTER_PRESS,
        "session replacement reused stale held ownership instead of sending a press");

  size_t shutdownCommands = control.commands.size();
  std::string shutdownStatus;
  host_webui_http::setStopHook(
      [&]() { shutdownStatus = shutter(R"({"action":"press"})").responseStatus; });
  webui.hostStopServer();
  check(shutdownStatus == "409 Conflict", "handler admitted a press during shutdown");
  check(control.commands.size() == shutdownCommands + 1
            && control.commands.back() == Control::CMD_SHUTTER_RELEASE,
        "shutdown did not release the replacement session without admitting its racing press");

  if (failures != 0)
    return 1;
  std::cout << "webui_handler_test: PASS\n";
  return 0;
}
