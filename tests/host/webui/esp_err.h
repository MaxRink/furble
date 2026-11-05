#ifndef FURBLE_HOST_WEBUI_ESP_ERR_H
#define FURBLE_HOST_WEBUI_ESP_ERR_H
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
constexpr esp_err_t ESP_ERR_NOT_SUPPORTED = 0x106;
constexpr esp_err_t ESP_ERR_HTTPD_ALLOC_MEM = 0xb007;
constexpr esp_err_t ESP_ERR_HTTPD_TASK = 0xb008;
inline const char *esp_err_to_name(esp_err_t value) {
  return value == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}
#endif
