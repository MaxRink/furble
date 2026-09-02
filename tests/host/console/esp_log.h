// Host esp_log shim for the console command suite.
//
// The guard matches tests/host/nimble/esp_log.h. Whichever of the two is
// reached first wins, and both define the same ESP_LOGx macros, so a
// translation unit never sees a macro redefinition. This one adds the log
// level API the 'log' command drives.
#ifndef FURBLE_HOST_ESP_LOG_H
#define FURBLE_HOST_ESP_LOG_H

#include <cstdint>

inline void furbleHostLog(const char *tag, const char *format, ...) {
  (void)tag;
  (void)format;
}

#define ESP_LOGD(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) furbleHostLog(tag, __VA_ARGS__)

typedef enum {
  ESP_LOG_NONE = 0,
  ESP_LOG_ERROR = 1,
  ESP_LOG_WARN = 2,
  ESP_LOG_INFO = 3,
  ESP_LOG_DEBUG = 4,
  ESP_LOG_VERBOSE = 5,
} esp_log_level_t;

void esp_log_level_set(const char *tag, esp_log_level_t level);

#endif
