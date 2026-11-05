#ifndef FURBLE_HOST_CRASH_ESP_LOG_H
#define FURBLE_HOST_CRASH_ESP_LOG_H

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

typedef int (*vprintf_like_t)(const char *, va_list);

/** Lines routed through the captured log hook, newest last. */
inline std::vector<std::string> &hostCrashLogLines(void) {
  static std::vector<std::string> lines;
  return lines;
}

inline vprintf_like_t &hostCrashLogVprintf(void) {
  static vprintf_like_t callback = nullptr;
  return callback;
}

inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t callback) {
  vprintf_like_t previous = hostCrashLogVprintf();
  hostCrashLogVprintf() = callback;
  return previous;
}

inline void furbleHostCrashLog(const char *format, ...) {
  char line[256] = {};
  va_list args;
  va_start(args, format);
  if (hostCrashLogVprintf() != nullptr) {
    va_list copy;
    va_copy(copy, args);
    hostCrashLogVprintf()(format, copy);
    va_end(copy);
    std::fputc('\n', stdout);
  }
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hostCrashLogLines().emplace_back(line);
}

#define ESP_LOGE(tag, format, ...) furbleHostCrashLog(format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) furbleHostCrashLog(format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) furbleHostCrashLog(format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) furbleHostCrashLog(format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) furbleHostCrashLog(format, ##__VA_ARGS__)

#endif
