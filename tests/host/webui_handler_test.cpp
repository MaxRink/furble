#include <iostream>
#include <map>
#include <string>

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleWebUI.h"
#include "esp_http_server.h"
#include "esp_timer.h"

namespace {

int failures = 0;
const std::map<std::string, std::string> validHeaders = {
    {"Authorization", "Basic ZnVyYmxlOmNvcnJlY3QgaG9yc2U="},
    {"Content-Type",  "application/json"                        },
    {"Host",          "furble.test"                             },
    {"Origin",        "https://furble.test"                     },
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
  Furble::Settings::password = "correct horse";
  Furble::Settings::passwordLoaded = true;
  check(webui.hostStartServer("test certificate", "test key"), "real WebUI server did not start");

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
  check(shutter(R"({"action":"press"})", wrongType).responseStatus
            == "415 Unsupported Media Type",
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

  control.deliveries.push_back({false, false});
  check(shutter(R"({"action":"release"})").responseStatus == "200 OK",
        "failed release was not retained for retry");
  const size_t beforeRejectedHold = control.commands.size();
  check(shutter(R"({"action":"hold","ms":100})").responseStatus == "409 Conflict",
        "new hold was accepted while release remained pending");
  check(control.commands.size() == beforeRejectedHold,
        "pending-release hold reached Control");
  control.deliveries.push_back({true, true});
  host_webui_timer::advance(20 * 1000);
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

  size_t shutdownCommands = control.commands.size();
  std::string shutdownStatus;
  host_webui_http::setStopHook([&]() {
    shutdownStatus = shutter(R"({"action":"press"})").responseStatus;
  });
  webui.hostStopServer();
  check(shutdownStatus == "409 Conflict", "handler admitted a press during shutdown");
  check(control.commands.size() == shutdownCommands,
        "shutdown-racing press reached Control after admission closed");

  if (failures != 0) return 1;
  std::cout << "webui_handler_test: PASS\n";
  return 0;
}
