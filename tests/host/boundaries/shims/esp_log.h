#ifndef FURBLE_BOUNDARY_ESP_LOG_H
#define FURBLE_BOUNDARY_ESP_LOG_H

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace BoundaryHost {
namespace detail {

// Production drivers log from worker tasks while the test thread polls the
// same storage. The store owns its lock so no caller can read the vector
// without synchronizing. Only boundaryLog() appends; tests use logContains().
struct LogStore {
  std::mutex mutex;
  std::vector<std::string> lines;
};

inline LogStore &logStore() {
  static LogStore store;
  return store;
}

}  // namespace detail
}  // namespace BoundaryHost

inline void boundaryLog(const char *level, const char *tag, const char *format, ...) {
  char line[512] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  std::fprintf(stderr, "[%s] %s: %s\n", level, tag == nullptr ? "boundary" : tag, line);
  auto &store = BoundaryHost::detail::logStore();
  const std::lock_guard<std::mutex> lock(store.mutex);
  store.lines.emplace_back(line);
}

#define ESP_LOGE(tag, format, ...) boundaryLog("E", tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) boundaryLog("W", tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) boundaryLog("I", tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) boundaryLog("D", tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) boundaryLog("V", tag, format, ##__VA_ARGS__)

#endif
