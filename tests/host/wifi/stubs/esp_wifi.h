#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
enum wifi_interface_t { WIFI_IF_STA };
enum wifi_mode_t { WIFI_MODE_STA };
enum wifi_ps_type_t { WIFI_PS_MIN_MODEM };
enum wifi_storage_t { WIFI_STORAGE_RAM };
struct wifi_init_config_t {};
struct wifi_sta_config_t {
  uint8_t ssid[32];
  uint8_t password[64];
  uint8_t bssid[6];
  bool bssid_set;
  uint8_t channel;
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
