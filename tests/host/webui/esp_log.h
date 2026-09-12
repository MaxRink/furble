#ifndef FURBLE_HOST_WEBUI_ESP_LOG_H
#define FURBLE_HOST_WEBUI_ESP_LOG_H
template <typename... Args>
inline void host_webui_log(const char *, const char *, Args &&...) {}
#define ESP_LOGD(...) host_webui_log(__VA_ARGS__)
#define ESP_LOGI(...) host_webui_log(__VA_ARGS__)
#define ESP_LOGW(...) host_webui_log(__VA_ARGS__)
#define ESP_LOGE(...) host_webui_log(__VA_ARGS__)
#endif
