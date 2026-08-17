#ifndef FURBLE_SIM_ESP_LOG_H
#define FURBLE_SIM_ESP_LOG_H

#include <cstdarg>
#include <cstdio>

inline void furble_sim_log(const char *level, const char *tag, const char *format, ...) {
  std::fprintf(stderr, "[%s] %s: ", level, tag == nullptr ? "sim" : tag);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
}

#define ESP_LOGE(tag, format, ...) furble_sim_log("E", tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) furble_sim_log("W", tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) furble_sim_log("I", tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) furble_sim_log("D", tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) furble_sim_log("V", tag, format, ##__VA_ARGS__)

#endif
