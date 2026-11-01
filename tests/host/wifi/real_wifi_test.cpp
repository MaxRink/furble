#include <cstdlib>
#include <cstring>
#include <iostream>
#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleWiFi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
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
esp_event_base_t WIFI_EVENT = "wifi";
esp_event_base_t IP_EVENT = "ip";
int esp_event_handler_register(esp_event_base_t, int32_t, esp_event_handler_t h, void *) {
  furble_wifi_handler = h;
  return 0;
}
esp_netif_t *esp_netif_create_default_wifi_sta() {
  static esp_netif_t n;
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
  // This first seam deliberately exercises the event callback directly; the
  // worker-task owner loop is deferred until the boundary fake models notify.
  Furble::WiFi::init();
  if (!furble_wifi_handler || !Furble::WiFi::setEnabled(false))
    return 1;
  furble_wifi_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
  if (Furble::WiFi::getStatus().connected)
    return 2;

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

  std::cout << "real wifi lifecycle: PASS\n";
  return 0;
}
