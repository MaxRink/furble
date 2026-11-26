#pragma once
// The host tests drive the production handlers, so a silent stub hides log-only
// behaviour. Warnings are counted so a test can pin that a path stays quiet.
inline int esp_log_warn_count = 0;
#define ESP_LOGE(...) \
  do {                \
  } while (false)
#define ESP_LOGW(...)        \
  do {                       \
    esp_log_warn_count += 1; \
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
