#include <cstdlib>
#include <cstring>
#include <iostream>
#include "FurbleControl.h"
#include "FurbleEthernet.h"
#include "FurbleSettings.h"
#include "FurbleWiFi.h"
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
int esp_netif_sntp_start() {
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
int esp_wifi_stop() {
  return 0;
}
int esp_wifi_deinit() {
  return 0;
}
int esp_wifi_disconnect() {
  return 0;
}
int esp_wifi_connect() {
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
  if (disconnect(WIFI_REASON_AUTH_FAIL) != 0)
    return 4;
  auto status = Furble::WiFi::getStatus();
  if (require(status.stopped, "auth failure did not stop the station")
      || require(status.disconnect_reason == WIFI_REASON_AUTH_FAIL,
                 "auth failure reason was not reported")
      || require(status.retry_ms == 0, "auth failure still scheduled a retry")) {
    return 5;
  }

  // A later connect clears the stopped state.
  if (!Furble::WiFi::connect() || Furble::WiFi::getStatus().stopped)
    return 6;

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
  Furble::Ethernet::stop();

  // P-027: only an idle Control state allows a join scan. Connect, active, and
  // disconnecting all mean the camera session owns the radio.
  for (uint8_t state = 0; state <= 5; state++) {
    const bool allowed = Furble::WiFi::hostJoinScanAllowed(state);
    if (require(allowed == (state == 0), "join scan predicate must allow only the idle state")) {
      return 20;
    }
  }

  std::cout << "real wifi lifecycle: PASS\n";
  return 0;
}
