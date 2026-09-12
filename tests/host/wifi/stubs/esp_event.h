#pragma once
#include <cstdint>
using esp_event_base_t = const char *;
using esp_event_handler_t = void (*)(void *, esp_event_base_t, int32_t, void *);
constexpr int32_t ESP_EVENT_ANY_ID = -1;
extern esp_event_handler_t furble_wifi_handler;
extern esp_event_base_t WIFI_EVENT;
extern esp_event_base_t IP_EVENT;
int esp_event_handler_register(esp_event_base_t, int32_t, esp_event_handler_t, void *);
#define IP_EVENT_STA_GOT_IP 1
#define WIFI_EVENT_STA_DISCONNECTED 2
