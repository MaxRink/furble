#include "mqtt_client.h"

#include <algorithm>
#include <memory>

struct esp_mqtt_client {
  esp_mqtt_client_config_t config = {};
  esp_event_handler_t handler = nullptr;
  void *handler_arg = nullptr;
  bool started = false;
};

namespace {

std::vector<host_mqtt::PublishedMessage> g_Published;
std::vector<host_mqtt::Subscription> g_Subscriptions;
esp_mqtt_client_handle_t g_Client = nullptr;
int g_StartCount = 0;
int g_NextMessageID = 1;

void emit(esp_mqtt_client_handle_t client,
          mqtt_event_id_t event_id,
          char *topic = nullptr,
          int topic_len = 0,
          char *data = nullptr,
          int data_len = 0) {
  if ((client == nullptr) || (client->handler == nullptr)) {
    return;
  }
  esp_mqtt_event_t event = {};
  event.event_id = event_id;
  event.client = client;
  event.topic = topic;
  event.topic_len = topic_len;
  event.data = data;
  event.data_len = data_len;
  event.total_data_len = data_len;
  event.current_data_offset = 0;
  client->handler(client->handler_arg, nullptr, 0, &event);
}

}  // namespace

namespace host_mqtt {

void reset(void) {
  g_Published.clear();
  g_Subscriptions.clear();
  g_StartCount = 0;
  g_NextMessageID = 1;
}

const std::vector<PublishedMessage> &published(void) {
  return g_Published;
}

const std::vector<Subscription> &subscriptions(void) {
  return g_Subscriptions;
}

int startCount(void) {
  return g_StartCount;
}

void deliver(const std::string &topic, const std::string &payload) {
  if (g_Client == nullptr) {
    return;
  }
  std::string topic_copy = topic;
  std::string payload_copy = payload;
  emit(g_Client, MQTT_EVENT_DATA, topic_copy.data(), static_cast<int>(topic_copy.size()),
       payload_copy.data(), static_cast<int>(payload_copy.size()));
}

}  // namespace host_mqtt

extern "C" esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config) {
  if (config == nullptr) {
    return nullptr;
  }
  auto *client = new esp_mqtt_client;
  client->config = *config;
  g_Client = client;
  return client;
}

extern "C" esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client,
                                                    mqtt_event_id_t,
                                                    esp_event_handler_t handler,
                                                    void *handler_arg) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->handler = handler;
  client->handler_arg = handler_arg;
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->started = true;
  g_StartCount++;
  emit(client, MQTT_EVENT_CONNECTED);
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->started = false;
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  if (g_Client == client) {
    g_Client = nullptr;
  }
  delete client;
  return ESP_OK;
}

extern "C" int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                                         const char *topic,
                                         int qos) {
  if ((client == nullptr) || (topic == nullptr)) {
    return -1;
  }
  g_Subscriptions.push_back({topic, qos});
  return g_NextMessageID++;
}

extern "C" int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client,
                                       const char *topic,
                                       const char *data,
                                       int len,
                                       int qos,
                                       int retain,
                                       int) {
  if ((client == nullptr) || (topic == nullptr) || (data == nullptr) || (len < 0)) {
    return -1;
  }
  g_Published.push_back({topic, std::string(data, static_cast<size_t>(len)), qos, retain != 0});
  return g_NextMessageID++;
}
