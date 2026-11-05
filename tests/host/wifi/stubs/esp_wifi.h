#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
enum wifi_interface_t { WIFI_IF_STA };
enum wifi_mode_t { WIFI_MODE_STA };
enum wifi_ps_type_t { WIFI_PS_MIN_MODEM };
enum wifi_storage_t { WIFI_STORAGE_RAM };
enum wifi_scan_method_t { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN = 1 };
enum wifi_sort_method_t { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY = 1 };
enum wifi_auth_mode_t {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
  WIFI_AUTH_WAPI_PSK,
  WIFI_AUTH_OWE,
  WIFI_AUTH_WPA3_ENT_192,
  WIFI_AUTH_WPA3_EXT_PSK,
  WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE,
  WIFI_AUTH_DPP,
  WIFI_AUTH_WPA3_ENTERPRISE,
  WIFI_AUTH_WPA2_WPA3_ENTERPRISE,
  WIFI_AUTH_WPA_ENTERPRISE,
  WIFI_AUTH_MAX
};
struct wifi_init_config_t {};
struct wifi_sta_config_t {
  uint8_t ssid[32];
  uint8_t password[64];
  uint8_t bssid[6];
  bool bssid_set;
  uint8_t channel;
  wifi_scan_method_t scan_method;
  wifi_sort_method_t sort_method;
  struct {
    wifi_auth_mode_t authmode;
    int8_t rssi;
  } threshold;
};
struct wifi_config_t {
  wifi_sta_config_t sta;
};
struct wifi_scan_config_t {
  bool show_hidden;
};
struct wifi_ap_record_t {
  uint8_t bssid[6];
  uint8_t primary;
  int8_t rssi;
  uint8_t ssid[33];
  wifi_auth_mode_t authmode;
};
// Disconnect reasons used by the station classification. Values match the
// ESP-IDF wifi_err_reason_t enum.
struct wifi_event_sta_disconnected_t {
  uint8_t reason;
};
enum {
  WIFI_REASON_AUTH_EXPIRE = 2,
  WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
  WIFI_REASON_BEACON_TIMEOUT = 200,
  WIFI_REASON_NO_AP_FOUND = 201,
  WIFI_REASON_AUTH_FAIL = 202,
  WIFI_REASON_ASSOC_FAIL = 203,
  WIFI_REASON_HANDSHAKE_TIMEOUT = 204,
  WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY = 210,
  WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 211,
  WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD = 212,
};
#define WIFI_INIT_CONFIG_DEFAULT() \
  wifi_init_config_t {}
int esp_wifi_init(const wifi_init_config_t *);
int esp_wifi_set_storage(wifi_storage_t);
int esp_wifi_set_mode(wifi_mode_t);
int esp_wifi_set_ps(wifi_ps_type_t);
int esp_wifi_start(void);
int esp_wifi_stop(void);
int esp_wifi_deinit(void);
int esp_wifi_disconnect(void);
int esp_wifi_connect(void);
int esp_wifi_set_config(wifi_interface_t, const wifi_config_t *);
int esp_wifi_scan_start(const wifi_scan_config_t *, bool);
int esp_wifi_scan_get_ap_num(uint16_t *);
int esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
