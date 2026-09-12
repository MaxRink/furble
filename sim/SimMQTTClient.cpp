#include "mqtt_client.h"

#if defined(FURBLE_SIM_MQTT) && FURBLE_SIM_MQTT

#include <charconv>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

#include <mosquitto.h>

namespace {

struct BrokerAddress {
  std::string host;
  int port = 1883;
};

bool parsePlainUri(const char *uri, BrokerAddress &address) {
  if (uri == nullptr) {
    return false;
  }

  constexpr char PREFIX[] = "mqtt://";
  const std::string value(uri);
  if (value.compare(0, sizeof(PREFIX) - 1, PREFIX) != 0) {
    return false;
  }

  const std::string authority = value.substr(sizeof(PREFIX) - 1);
  if (authority.empty() || authority.find('/') != std::string::npos) {
    return false;
  }

  std::string host = authority;
  std::string port_text;
  if (authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close == std::string::npos || close == 1) {
      return false;
    }
    host = authority.substr(1, close - 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') {
        return false;
      }
      port_text = authority.substr(close + 2);
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      if (authority.find(':') != colon) {
        return false;
      }
      host = authority.substr(0, colon);
      port_text = authority.substr(colon + 1);
    }
  }

  if (host.empty()) {
    return false;
  }
  if (!port_text.empty()) {
    unsigned int port = 0;
    const auto result =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (result.ec != std::errc() || result.ptr != port_text.data() + port_text.size() || port == 0
        || port > UINT16_MAX) {
      return false;
    }
    address.port = static_cast<int>(port);
  }
  address.host = std::move(host);
  return true;
}

}  // namespace

struct esp_mqtt_client {
  std::string uri;
  std::string client_id;
  std::string username;
  std::string password;
  std::string will_topic;
  std::string will_payload;
  int will_qos = 0;
  bool will_retain = false;
  int keepalive = 60;
  bool clean_session = true;

  mosquitto *mosq = nullptr;
  esp_event_handler_t handler = nullptr;
  void *handler_arg = nullptr;
  mqtt_event_id_t event_filter = MQTT_EVENT_ANY;
  std::mutex mutex;
  std::condition_variable operations_drained;
  size_t operations = 0;
  bool started = false;
  bool stopping = false;

  bool beginOperation(mosquitto *&transport) {
    const std::lock_guard<std::mutex> lock(mutex);
    if (!started || stopping || mosq == nullptr) {
      return false;
    }
    ++operations;
    transport = mosq;
    return true;
  }

  void endOperation(void) {
    const std::lock_guard<std::mutex> lock(mutex);
    --operations;
    if (operations == 0) {
      operations_drained.notify_all();
    }
  }

  void dispatch(esp_mqtt_event_t &event, esp_mqtt_error_codes_t *error = nullptr) {
    esp_event_handler_t callback = nullptr;
    void *callback_arg = nullptr;
    {
      const std::lock_guard<std::mutex> lock(mutex);
      if (!started || handler == nullptr
          || (event_filter != MQTT_EVENT_ANY && event_filter != event.event_id)) {
        return;
      }
      callback = handler;
      callback_arg = handler_arg;
    }
    event.error_handle = error;
    callback(callback_arg, nullptr, event.event_id, &event);
  }
};

namespace {

void onConnect(mosquitto *, void *userdata, int result) {
  auto *client = static_cast<esp_mqtt_client *>(userdata);
  esp_mqtt_event_t event = {};
  event.event_id = result == 0 ? MQTT_EVENT_CONNECTED : MQTT_EVENT_ERROR;
  event.client = client;
  esp_mqtt_error_codes_t error = {result};
  client->dispatch(event, result == 0 ? nullptr : &error);
}

void onDisconnect(mosquitto *, void *userdata, int result) {
  auto *client = static_cast<esp_mqtt_client *>(userdata);
  esp_mqtt_event_t event = {};
  event.event_id = MQTT_EVENT_DISCONNECTED;
  event.client = client;
  esp_mqtt_error_codes_t error = {result};
  client->dispatch(event, &error);
}

void onMessage(mosquitto *, void *userdata, const mosquitto_message *message) {
  auto *client = static_cast<esp_mqtt_client *>(userdata);
  if (message == nullptr || message->topic == nullptr || std::strlen(message->topic) > INT_MAX
      || message->payloadlen < 0) {
    return;
  }

  char empty_payload = '\0';
  auto *payload =
      message->payload == nullptr ? &empty_payload : static_cast<char *>(message->payload);
  esp_mqtt_event_t event = {};
  event.event_id = MQTT_EVENT_DATA;
  event.client = client;
  event.topic = message->topic;
  event.topic_len = static_cast<int>(std::strlen(message->topic));
  event.data = payload;
  event.data_len = message->payloadlen;
  event.total_data_len = message->payloadlen;
  event.current_data_offset = 0;
  event.qos = message->qos;
  event.retain = message->retain;
  event.msg_id = message->mid;
  client->dispatch(event);
}

void onPublish(mosquitto *, void *userdata, int message_id) {
  auto *client = static_cast<esp_mqtt_client *>(userdata);
  esp_mqtt_event_t event = {};
  event.event_id = MQTT_EVENT_PUBLISHED;
  event.client = client;
  event.msg_id = message_id;
  client->dispatch(event);
}

void initialiseMosquitto(void) {
  mosquitto_lib_init();
}

}  // namespace

extern "C" esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config) {
  if (config == nullptr || config->broker.address.uri == nullptr) {
    return nullptr;
  }

  static std::once_flag library_once;
  std::call_once(library_once, initialiseMosquitto);

  auto *client = new esp_mqtt_client;
  client->uri = config->broker.address.uri;
  if (config->credentials.client_id != nullptr) {
    client->client_id = config->credentials.client_id;
  }
  if (config->credentials.username != nullptr) {
    client->username = config->credentials.username;
  }
  if (config->credentials.authentication.password != nullptr) {
    client->password = config->credentials.authentication.password;
  }
  if (config->session.last_will.topic != nullptr) {
    client->will_topic = config->session.last_will.topic;
  }
  if (config->session.last_will.msg != nullptr && config->session.last_will.msg_len >= 0) {
    client->will_payload.assign(config->session.last_will.msg,
                                static_cast<size_t>(config->session.last_will.msg_len));
  }
  client->will_qos = config->session.last_will.qos;
  client->will_retain = config->session.last_will.retain;
  client->keepalive = config->session.keepalive > 0 ? config->session.keepalive : 60;
  client->clean_session = !config->session.disable_clean_session;

  client->mosq = mosquitto_new(client->client_id.empty() ? nullptr : client->client_id.c_str(),
                               client->clean_session, client);
  if (client->mosq == nullptr) {
    delete client;
    return nullptr;
  }
  mosquitto_connect_callback_set(client->mosq, onConnect);
  mosquitto_disconnect_callback_set(client->mosq, onDisconnect);
  mosquitto_message_callback_set(client->mosq, onMessage);
  mosquitto_publish_callback_set(client->mosq, onPublish);
  if (!client->username.empty()
      && mosquitto_username_pw_set(client->mosq, client->username.c_str(),
                                   client->password.empty() ? nullptr : client->password.c_str())
             != MOSQ_ERR_SUCCESS) {
    mosquitto_destroy(client->mosq);
    delete client;
    return nullptr;
  }
  if (!client->will_topic.empty()
      && mosquitto_will_set(client->mosq, client->will_topic.c_str(),
                            static_cast<int>(client->will_payload.size()),
                            client->will_payload.data(), client->will_qos, client->will_retain)
             != MOSQ_ERR_SUCCESS) {
    mosquitto_destroy(client->mosq);
    delete client;
    return nullptr;
  }
  return client;
}

extern "C" esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client,
                                                    mqtt_event_id_t event,
                                                    esp_event_handler_t handler,
                                                    void *handler_arg) {
  if (client == nullptr || handler == nullptr) {
    return ESP_FAIL;
  }
  const std::lock_guard<std::mutex> lock(client->mutex);
  client->event_filter = event;
  client->handler = handler;
  client->handler_arg = handler_arg;
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  BrokerAddress address;
  if (!parsePlainUri(client->uri.c_str(), address)) {
    return ESP_FAIL;
  }

  {
    const std::lock_guard<std::mutex> lock(client->mutex);
    if (client->started || client->mosq == nullptr) {
      return ESP_ERR_INVALID_STATE;
    }
  }
  if (mosquitto_loop_start(client->mosq) != MOSQ_ERR_SUCCESS) {
    return ESP_FAIL;
  }
  {
    const std::lock_guard<std::mutex> lock(client->mutex);
    client->started = true;
    client->stopping = false;
  }
  if (mosquitto_connect_async(client->mosq, address.host.c_str(), address.port, client->keepalive)
      != MOSQ_ERR_SUCCESS) {
    {
      const std::lock_guard<std::mutex> lock(client->mutex);
      client->started = false;
      client->stopping = true;
    }
    (void)mosquitto_loop_stop(client->mosq, true);
    {
      const std::lock_guard<std::mutex> lock(client->mutex);
      client->stopping = false;
    }
    return ESP_FAIL;
  }
  return ESP_OK;
}

extern "C" esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  {
    std::unique_lock<std::mutex> lock(client->mutex);
    if (!client->started) {
      return ESP_OK;
    }
    client->started = false;
    client->stopping = true;
    client->operations_drained.wait(lock, [client]() { return client->operations == 0; });
  }
  (void)mosquitto_disconnect(client->mosq);
  const int result = mosquitto_loop_stop(client->mosq, true);
  {
    const std::lock_guard<std::mutex> lock(client->mutex);
    client->stopping = false;
  }
  return result == MOSQ_ERR_SUCCESS ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  const esp_err_t stop_result = esp_mqtt_client_stop(client);
  if (stop_result != ESP_OK) {
    return stop_result;
  }
  mosquitto_destroy(client->mosq);
  delete client;
  return ESP_OK;
}

extern "C" int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                                         const char *topic,
                                         int qos) {
  if (client == nullptr || topic == nullptr) {
    return -1;
  }
  mosquitto *transport = nullptr;
  if (!client->beginOperation(transport)) {
    return -1;
  }
  int message_id = -1;
  const int result = mosquitto_subscribe(transport, &message_id, topic, qos);
  client->endOperation();
  return result == MOSQ_ERR_SUCCESS ? message_id : -1;
}

extern "C" int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client,
                                       const char *topic,
                                       const char *data,
                                       int len,
                                       int qos,
                                       int retain,
                                       int store) {
  (void)store;
  if (client == nullptr || topic == nullptr || len < 0 || (data == nullptr && len != 0)) {
    return -1;
  }
  mosquitto *transport = nullptr;
  if (!client->beginOperation(transport)) {
    return -1;
  }
  int message_id = -1;
  const int result = mosquitto_publish(transport, &message_id, topic, len, data, qos, retain);
  client->endOperation();
  return result == MOSQ_ERR_SUCCESS ? message_id : -1;
}

#endif
