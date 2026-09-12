#pragma once
#define ESP_LOGE(...) \
  do {                \
  } while (false)
#define ESP_LOGW(...) \
  do {                \
  } while (false)
#define ESP_LOGI(...) \
  do {                \
  } while (false)
#define ESP_LOGV(...) \
  do {                \
  } while (false)
#define ESP_ERROR_CHECK(expr) \
  do {                        \
    (void)(expr);             \
  } while (false)
inline const char *esp_err_to_name(int) {
  return "stub";
}
