#ifndef FURBLE_HOST_ESP_LOG_H
#define FURBLE_HOST_ESP_LOG_H

inline void furbleHostLog(const char *tag, const char *format, ...) {
  (void)tag;
  (void)format;
}

#define ESP_LOGD(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) furbleHostLog(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) furbleHostLog(tag, __VA_ARGS__)

#endif
