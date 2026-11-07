#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "FurbleControl.h"
#include "FurbleEthernet.h"
#include "FurbleSettings.h"
#include "FurbleWiFi.h"
#include "Preferences.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "eth/MockEthNetif.h"
#include "freertos/task.h"

namespace Furble {
Control &Control::getInstance() {
  static Control c;
  return c;
}
Control::state_t Control::getState() const {
  return STATE_IDLE;
}
bool Settings::validNetworkString(type_t, const std::string &v) {
  return !v.empty();
}
}  // namespace Furble
esp_event_handler_t furble_wifi_handler = nullptr;
int event_handler_registrations = 0;
int netif_creations = 0;
int task_creations = 0;
esp_event_base_t WIFI_EVENT = "wifi";
esp_event_base_t IP_EVENT = "ip";
int esp_event_handler_register(esp_event_base_t, int32_t, esp_event_handler_t h, void *) {
  furble_wifi_handler = h;
  event_handler_registrations++;
  return 0;
}
esp_netif_t *esp_netif_create_default_wifi_sta() {
  static esp_netif_t n;
  netif_creations++;
  return &n;
}
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *i) {
  i->ip.addr = 0x0100007f;
  return 0;
}
const char *esp_ip4addr_ntoa(const esp_ip4_addr_t *, char *b, int) {
  return std::strcpy(b, "127.0.0.1");
}
int esp_netif_sntp_init(const esp_sntp_config_t *) {
  return 0;
}
int sntp_starts = 0;
int esp_netif_sntp_start() {
  sntp_starts++;
  return 0;
}
void esp_netif_sntp_deinit() {}
int esp_wifi_init(const wifi_init_config_t *) {
  return 0;
}
int esp_wifi_set_storage(wifi_storage_t) {
  return 0;
}
int esp_wifi_set_mode(wifi_mode_t) {
  return 0;
}
int esp_wifi_set_ps(wifi_ps_type_t) {
  return 0;
}
int esp_wifi_start() {
  return 0;
}
int wifi_stop_calls = 0;
int wifi_deinit_calls = 0;
bool disconnect_during_connect = false;
int esp_wifi_stop() {
  wifi_stop_calls++;
  return 0;
}
int esp_wifi_deinit() {
  wifi_deinit_calls++;
  return 0;
}
int esp_wifi_disconnect() {
  return 0;
}
int esp_wifi_connect() {
  if (disconnect_during_connect) {
    disconnect_during_connect = false;
    wifi_event_sta_disconnected_t event = {WIFI_REASON_BEACON_TIMEOUT};
    if (furble_wifi_handler != nullptr) {
      furble_wifi_handler(nullptr, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
    }
  }
  return 0;
}
int esp_wifi_set_config(wifi_interface_t, const wifi_config_t *) {
  return 0;
}
int last_show_hidden = -1;
int esp_wifi_scan_start(const wifi_scan_config_t *config, bool) {
  last_show_hidden = config != nullptr ? (config->show_hidden ? 1 : 0) : -1;
  return 0;
}
int esp_wifi_scan_get_ap_num(uint16_t *n) {
  *n = 0;
  return 0;
}
int esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *) {
  return 0;
}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) {
  return 0;
}
BaseType_t xTaskCreate(void (*fn)(void *),
                       const char *,
                       uint32_t,
                       void *arg,
                       UBaseType_t,
                       TaskHandle_t *out) {
  task_creations++;
  *out = nullptr;
  (void)fn;
  (void)arg;
  return pdPASS;
}
BaseType_t xTaskNotify(TaskHandle_t, uint32_t, int) {
  return pdPASS;
}
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, TickType_t) {
  return 0;
}

namespace {

int require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

int disconnect(uint8_t reason) {
  wifi_event_sta_disconnected_t event = {reason};
  if (furble_wifi_handler == nullptr) {
    return 1;
  }
  furble_wifi_handler(nullptr, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
  return 0;
}

}  // namespace

int main() {
  // P-034: a disabled Wi-Fi setting must not create the netif, the event
  // handlers, or the worker at boot. The module starts on the first enable.
  Furble::wifiEnabledSetting = false;
  Furble::WiFi::init();
  if (require(netif_creations == 0, "disabled init created a station netif")
      || require(event_handler_registrations == 0, "disabled init registered event handlers")
      || require(task_creations == 0, "disabled init created the Wi-Fi worker")) {
    return 2;
  }
  Furble::wifiEnabledSetting = true;
  if (require(Furble::WiFi::connect(), "enable after a disabled init did not start")
      || require(netif_creations == 1, "lazy start did not create exactly one netif")
      || require(event_handler_registrations == 2, "lazy start did not register two handlers")
      || require(task_creations == 1, "lazy start did not create one worker")) {
    return 3;
  }
  if (!furble_wifi_handler || !Furble::WiFi::setEnabled(false))
    return 4;
  furble_wifi_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
  if (Furble::WiFi::getStatus().connected)
    return 5;

  // P-028: an authentication failure is permanent. The station must stop and
  // report the reason instead of retrying forever.
  if (!Furble::WiFi::connect())
    return 3;
  // Exercise the production connect request so the driver is genuinely
  // initialized before the terminal-cleanup assertion.
  if (require(Furble::WiFi::hostConnectAttempt(), "the production connect request failed")
      || require(Furble::WiFi::getStatus().driver,
                 "the connect request did not start the driver")) {
    return 30;
  }
  if (disconnect(WIFI_REASON_AUTH_FAIL) != 0)
    return 4;
  auto status = Furble::WiFi::getStatus();
  if (require(status.stopped, "auth failure did not stop the station")
      || require(status.disconnect_reason == WIFI_REASON_AUTH_FAIL,
                 "auth failure reason was not reported")
      || require(status.retry_ms == 0, "auth failure still scheduled a retry")
      || require(status.driver, "the terminal disconnect stopped the driver before cleanup")) {
    return 5;
  }
  // The worker's terminal cleanup must release the driver after a terminal
  // disconnect; the connected-state wait used to continue past it.
  const int stopBefore = wifi_stop_calls;
  const int deinitBefore = wifi_deinit_calls;
  Furble::WiFi::hostServiceTerminalStop();
  if (require(!Furble::WiFi::getStatus().driver,
              "a terminal disconnect left the Wi-Fi driver initialized")
      || require(wifi_stop_calls > stopBefore, "terminal cleanup did not stop the station")
      || require(wifi_deinit_calls > deinitBefore, "terminal cleanup did not deinit the driver")) {
    return 29;
  }

  // A later connect clears the stopped state.
  if (!Furble::WiFi::connect() || Furble::WiFi::getStatus().stopped)
    return 6;

  // A retryable disconnect delivered inside the connect request must end the
  // join wait immediately. Snapshotting the lifecycle counter after the call
  // would wait the full 15 s deadline and count the failure twice.
  const uint32_t lifecycle = Furble::WiFi::hostLifecycleEvent();
  disconnect_during_connect = true;
  const auto joinStart = std::chrono::steady_clock::now();
  const bool joined = Furble::WiFi::hostConnectAndWaitForJoin();
  const auto joinMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - joinStart)
                          .count();
  if (require(!joined, "the join wait treated a mid-request disconnect as a successful join")
      || require(Furble::WiFi::hostLifecycleEvent() != lifecycle,
                 "the connect request delivered no disconnect")
      || require(joinMs < 5000, "the join wait ran its full deadline after a lifecycle event")
      || require(Furble::WiFi::getStatus().retry_ms == 1000,
                 "the join wait counted the mid-request disconnect twice")) {
    return 32;
  }
  // A lifecycle notification is still pending from the mid-request disconnect.
  // The retry wait must keep its absolute deadline instead of treating the
  // first wake as the end of the backoff.
  const uint32_t waitedMs = Furble::WiFi::hostWaitForRetry();
  if (require(waitedMs >= 900, "the retry wait skipped its scheduled backoff")) {
    return 34;
  }
  // The mid-request disconnect counted one attempt; reset before the bounded
  // no-AP sequence so its threshold starts from zero.
  if (!Furble::WiFi::connect()) {
    return 33;
  }

  // P-028: a missing AP is bounded, not permanent. Six attempts stop it.
  for (int i = 0; i < 5; i++) {
    if (disconnect(WIFI_REASON_NO_AP_FOUND) != 0)
      return 7;
  }
  if (require(!Furble::WiFi::getStatus().stopped,
              "no-AP stopped before the bounded attempt count")) {
    return 8;
  }
  if (disconnect(WIFI_REASON_NO_AP_FOUND) != 0)
    return 9;
  if (require(Furble::WiFi::getStatus().stopped,
              "no-AP did not stop at the bounded attempt count")) {
    return 10;
  }

  // P-028: a handshake timeout is transient, not a confirmed credential error.
  if (!Furble::WiFi::connect())
    return 15;
  if (disconnect(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) != 0)
    return 16;
  status = Furble::WiFi::getStatus();
  if (require(!status.stopped, "a single handshake timeout stopped the station")
      || require(status.retry_ms > 0, "a handshake timeout scheduled no retry")) {
    return 17;
  }

  // P-033: transient failures back off to the raised five-minute cap.
  if (!Furble::WiFi::connect())
    return 11;
  for (int i = 0; i < 10; i++) {
    if (disconnect(WIFI_REASON_BEACON_TIMEOUT) != 0)
      return 12;
  }
  status = Furble::WiFi::getStatus();
  if (require(status.retry_ms == 300000, "transient retry did not reach the 300 s cap")
      || require(status.stopped, "transient failures did not stop after the attempt bound")) {
    return 13;
  }

  // P-029: the join scan never asks for hidden results when the SSID is known.
  if (require(Furble::WiFi::hostScanForNetwork() == false, "empty scan results should return false")
      || require(last_show_hidden == 0, "join scan requested hidden results")) {
    return 14;
  }

  // P-032: a wired address defers the station. When the wire drops, the
  // service check re-arms the connection request.
  FurbleHost::MockEthNetif wired;
  if (!Furble::Ethernet::init(wired))
    return 15;
  wired.emitLinkUp();
  wired.emitGotIp("192.0.2.10");
  if (!Furble::Ethernet::isConnected())
    return 16;
  if (!Furble::WiFi::connect())
    return 17;
  if (require(Furble::WiFi::hostDeferredForWired(), "a wired address did not defer the station")) {
    return 18;
  }
  wired.emitLinkDown();
  if (require(Furble::WiFi::hostServiceWired(), "wire drop did not re-arm the station")
      || require(!Furble::WiFi::hostDeferredForWired(), "deferral survived the wire drop")) {
    return 19;
  }
  // A cable plugged in after the station joined must hand the network back.
  Furble::Ethernet::init(wired);
  wired.emitLinkUp();
  wired.emitGotIp("192.0.2.11");
  const int beginsBefore = Furble::preferencesBegins;
  const int sntpBefore = sntp_starts;
  furble_wifi_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
  if (require(Furble::WiFi::getStatus().connected, "the station reports connected")) {
    return 21;
  }
  // The handler runs on the 2,304-byte default event task: it must not open
  // NVS or start SNTP. The worker does both.
  if (require(Furble::preferencesBegins == beginsBefore, "GOT_IP opened NVS on the event task")
      || require(sntp_starts == sntpBefore, "GOT_IP started SNTP on the event task")) {
    return 40;
  }
  Furble::WiFi::hostServiceEventStorage();
  if (require(Furble::preferencesBegins == beginsBefore + 1,
              "the worker did not persist the access point")
      || require(sntp_starts == sntpBefore + 1, "the worker did not start SNTP")) {
    return 41;
  }
  Furble::WiFi::hostServiceEventStorage();
  if (require(Furble::preferencesBegins == beginsBefore + 1, "the deferred storage ran twice")) {
    return 42;
  }
  // A clear after the event and before the worker cancels the queued save, so
  // the old access point is not written back.
  furble_wifi_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
  Furble::WiFi::clearRememberedAccessPoint();
  const int beginsAfterClear = Furble::preferencesBegins;
  Furble::WiFi::hostServiceEventStorage();
  if (require(Furble::preferencesBegins == beginsAfterClear,
              "a deferred save ran after the access point was cleared")) {
    return 43;
  }
  if (require(Furble::WiFi::hostServiceConnectedWired(),
              "a wired address did not take over a connected station")
      || require(!Furble::WiFi::getStatus().connected,
                 "the station stayed connected after the wire came up")
      || require(Furble::WiFi::hostDeferredForWired(),
                 "the connected takeover did not defer the station")) {
    return 22;
  }
  Furble::Ethernet::stop();
  if (require(Furble::WiFi::hostServiceWired(), "the wire drop did not resume the station")) {
    return 23;
  }

  // An explicit disconnect after a takeover cancels the automatic resume. The
  // takeover publishes its state in the same critical section that a manual
  // disconnect clears, so cable loss must not restart a station the operator
  // stopped.
  if (!Furble::WiFi::connect()) {
    return 25;
  }
  furble_wifi_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
  Furble::Ethernet::init(wired);
  wired.emitLinkUp();
  wired.emitGotIp("192.0.2.12");
  if (require(Furble::WiFi::hostServiceConnectedWired(),
              "the wired takeover did not run for the cancel test")
      || require(Furble::WiFi::hostResumeAfterWired(), "the takeover did not schedule a resume")) {
    return 26;
  }
  Furble::WiFi::disconnect();
  if (require(!Furble::WiFi::hostDeferredForWired(),
              "a manual disconnect left the wired deferral set")
      || require(!Furble::WiFi::hostResumeAfterWired(),
                 "a manual disconnect did not cancel the wired resume")) {
    return 27;
  }
  Furble::Ethernet::stop();
  if (require(!Furble::WiFi::hostServiceWired(),
              "cable loss restarted the station after a manual disconnect")) {
    return 28;
  }

  // The coexistence retry cap applies to the common scheduling path, so a
  // disconnect event after esp_wifi_connect() cannot grow it past ten seconds.
  if (require(Furble::WiFi::hostCoexRetryCap() == 10000,
              "the coexistence retry cap did not clamp the backoff")) {
    return 24;
  }
  // Restore the normal retry state for any later assertions.
  Furble::WiFi::connect();

  // P-027: only an idle Control state allows a join scan. Connect, active, and
  // disconnecting all mean the camera session owns the radio.
  for (uint8_t state = 0; state <= 5; state++) {
    const bool allowed = Furble::WiFi::hostJoinScanAllowed(state);
    if (require(allowed == (state == 0), "join scan predicate must allow only the idle state")) {
      return 20;
    }
  }

  std::cout << "wifi.fallback wired_defer=1 wired_resume=1\n";
  std::cout << "real wifi lifecycle: PASS\n";
  return 0;
}
