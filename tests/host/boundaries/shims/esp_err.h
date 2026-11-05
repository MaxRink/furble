#ifndef FURBLE_BOUNDARY_ESP_ERR_H
#define FURBLE_BOUNDARY_ESP_ERR_H

#include <cstdint>

using esp_err_t = int32_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT 0x107
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif
#ifndef ESP_ERR_NVS_NO_FREE_PAGES
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#endif
#ifndef ESP_ERR_NVS_NEW_VERSION_FOUND
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#endif

inline const char *esp_err_to_name(esp_err_t error) {
  switch (error) {
    case ESP_OK:
      return "ESP_OK";
    case ESP_FAIL:
      return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
      return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_STATE:
      return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_ARG:
      return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_TIMEOUT:
      return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_FOUND:
      return "ESP_ERR_NOT_FOUND";
    default:
      return "ESP_ERR_UNKNOWN";
  }
}

#endif
