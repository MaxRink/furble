#include "mqtt_client.h"

#include <algorithm>
#include <memory>
#include <vector>

struct esp_mqtt_client {
  esp_mqtt_client_config_t config = {};
  esp_event_handler_t handler = nullptr;
  void *handler_arg = nullptr;
  bool started = false;
  bool connected = false;
};

namespace {

std::vector<host_mqtt::PublishedMessage> g_Published;
std::vector<host_mqtt::Subscription> g_Subscriptions;
std::vector<host_mqtt::PublishedMessage> g_Retained;
esp_mqtt_client_handle_t g_Client = nullptr;
int g_StartCount = 0;
int g_NextMessageID = 1;

bool topicMatches(const std::string &filter, const std::string &topic) {
  auto levels = [](const std::string &value) {
    std::vector<std::string> result;
    size_t start = 0;
    while (true) {
      const size_t end = value.find('/', start);
      result.push_back(
          value.substr(start, end == std::string::npos ? std::string::npos : end - start));
      if (end == std::string::npos) {
        return result;
      }
      start = end + 1;
    }
  };

  const auto filter_levels = levels(filter);
  const auto topic_levels = levels(topic);
  if (!topic.empty() && (topic.front() == '$')
      && ((filter_levels.front() == "+") || (filter_levels.front() == "#"))) {
    return false;
  }
  size_t n = 0;
  for (; n < filter_levels.size(); n++) {
    if (filter_levels[n] == "#") {
      return n == filter_levels.size() - 1;
    }
    if (n >= topic_levels.size()
        || ((filter_levels[n] != "+") && (filter_levels[n] != topic_levels[n]))) {
      return false;
    }
  }
  return n == topic_levels.size();
}

bool validFilter(const std::string &filter) {
  if (filter.empty()) {
    return false;
  }
  size_t start = 0;
  while (true) {
    const size_t end = filter.find('/', start);
    const std::string level =
        filter.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if ((level.find('+') != std::string::npos) && (level != "+")) {
      return false;
    }
    if ((level.find('#') != std::string::npos) && ((level != "#") || (end != std::string::npos))) {
      return false;
    }
    if (end == std::string::npos) {
      return true;
    }
    start = end + 1;
  }
}

bool subscribed(const std::string &topic) {
  return std::any_of(g_Subscriptions.begin(), g_Subscriptions.end(), [&](const auto &subscription) {
    return topicMatches(subscription.topic, topic);
  });
}

void emit(esp_mqtt_client_handle_t client,
          mqtt_event_id_t event_id,
          char *topic = nullptr,
          int topic_len = 0,
          char *data = nullptr,
          int data_len = 0,
          int current_data_offset = 0,
          int total_data_len = -1) {
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
  event.total_data_len = total_data_len < 0 ? data_len : total_data_len;
  event.current_data_offset = current_data_offset;
  client->handler(client->handler_arg, nullptr, 0, &event);
}

void deliverToClient(esp_mqtt_client_handle_t client,
                     const std::string &topic,
                     const std::string &payload,
                     int current_data_offset = 0,
                     int total_data_len = -1) {
  if ((client == nullptr) || !client->started || !client->connected || !subscribed(topic)) {
    return;
  }
  std::string topic_copy = topic;
  std::string payload_copy = payload;
  emit(client, MQTT_EVENT_DATA, topic_copy.data(), static_cast<int>(topic_copy.size()),
       payload_copy.data(), static_cast<int>(payload_copy.size()), current_data_offset,
       total_data_len);
}

void retainMessage(const std::string &topic, const std::string &payload, int qos, bool retain) {
  if (!retain) {
    return;
  }
  const auto found = std::find_if(g_Retained.begin(), g_Retained.end(),
                                  [&](const auto &message) { return message.topic == topic; });
  if (payload.empty()) {
    if (found != g_Retained.end()) {
      g_Retained.erase(found);
    }
    return;
  }
  if (found == g_Retained.end()) {
    g_Retained.push_back({topic, payload, qos, true});
  } else {
    *found = {topic, payload, qos, true};
  }
}

}  // namespace

namespace host_mqtt {

void reset(void) {
  // Tests must stop the production client before reset. The raw delete keeps
  // a failed fixture from leaking a client into the next fixture.
  delete g_Client;
  g_Client = nullptr;
  g_Published.clear();
  g_Subscriptions.clear();
  g_Retained.clear();
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

bool hasRetained(const std::string &topic) {
  return std::any_of(g_Retained.begin(), g_Retained.end(),
                     [&](const auto &message) { return message.topic == topic; });
}

void deliver(const std::string &topic, const std::string &payload) {
  deliverToClient(g_Client, topic, payload);
}

void deliverFragmented(const std::string &topic,
                       const std::string &payload,
                       int current_data_offset,
                       int total_data_len) {
  deliverToClient(g_Client, topic, payload, current_data_offset, total_data_len);
}

void brokerPublish(const std::string &topic, const std::string &payload, int qos, bool retain) {
  retainMessage(topic, payload, qos, retain);
  deliverToClient(g_Client, topic, payload);
}

void dropConnection(void) {
  if ((g_Client == nullptr) || !g_Client->started || !g_Client->connected) {
    return;
  }
  g_Client->connected = false;
  // The production config uses clean sessions. A link loss discards the
  // broker-side subscriptions, so MQTT_EVENT_CONNECTED must recreate them.
  g_Subscriptions.clear();
  emit(g_Client, MQTT_EVENT_DISCONNECTED);
}

void restoreConnection(void) {
  if ((g_Client == nullptr) || !g_Client->started || g_Client->connected) {
    return;
  }
  g_Client->connected = true;
  emit(g_Client, MQTT_EVENT_CONNECTED);
}

}  // namespace host_mqtt

extern "C" esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config) {
  if ((config == nullptr) || (g_Client != nullptr)) {
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
  client->connected = true;
  g_StartCount++;
  emit(client, MQTT_EVENT_CONNECTED);
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->started = false;
  client->connected = false;
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  if (g_Client == client) {
    g_Client = nullptr;
    g_Subscriptions.clear();
  }
  delete client;
  return ESP_OK;
}

extern "C" int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                                         const char *topic,
                                         int qos) {
  if ((client == nullptr) || (topic == nullptr) || !validFilter(topic)) {
    return -1;
  }
  const auto existing =
      std::find_if(g_Subscriptions.begin(), g_Subscriptions.end(), [&](const auto &subscription) {
        return subscription.topic == topic && subscription.qos == qos;
      });
  if (existing == g_Subscriptions.end()) {
    g_Subscriptions.push_back({topic, qos});
  }
  if (client->started && client->connected) {
    const auto retained = g_Retained;
    for (const auto &message : retained) {
      if (topicMatches(topic, message.topic)) {
        deliverToClient(client, message.topic, message.payload);
      }
    }
  }
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
  if (!client->started || !client->connected) {
    return -1;
  }
  const std::string payload(data, static_cast<size_t>(len));
  retainMessage(topic, payload, qos, retain != 0);
  g_Published.push_back({topic, std::string(data, static_cast<size_t>(len)), qos, retain != 0});
  return g_NextMessageID++;
}
