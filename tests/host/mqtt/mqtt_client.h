#ifndef FURBLE_HOST_MQTT_CLIENT_H
#define FURBLE_HOST_MQTT_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_event.h"

struct esp_mqtt_client;
using esp_mqtt_client_handle_t = esp_mqtt_client *;

enum mqtt_event_id_t {
  MQTT_EVENT_ANY = -1,
  MQTT_EVENT_CONNECTED = 1,
  MQTT_EVENT_DISCONNECTED,
  MQTT_EVENT_DATA,
  MQTT_EVENT_ERROR,
};

constexpr int MQTT_PROTOCOL_V_3_1_1 = 4;

struct esp_mqtt_error_codes_t {
  int error_type;
};

struct esp_mqtt_event_t {
  mqtt_event_id_t event_id;
  esp_mqtt_client_handle_t client;
  char *topic;
  int topic_len;
  char *data;
  int data_len;
  int total_data_len;
  int current_data_offset;
  esp_mqtt_error_codes_t *error_handle;
};
using esp_mqtt_event_handle_t = esp_mqtt_event_t *;
using esp_event_handler_t = void (*)(void *, esp_event_base_t, int32_t, void *);

struct esp_mqtt_client_config_t {
  struct {
    struct {
      const char *uri;
    } address;
    struct {
      esp_err_t (*crt_bundle_attach)(void *);
    } verification;
  } broker;
  struct {
    const char *client_id;
    const char *username;
    struct {
      const char *password;
    } authentication;
  } credentials;
  struct {
    struct {
      const char *topic;
      const char *msg;
      int msg_len;
      int qos;
      int retain;
    } last_will;
    int keepalive;
    bool disable_clean_session;
    int protocol_ver;
  } session;
  struct {
    int reconnect_timeout_ms;
    int timeout_ms;
  } network;
  struct {
    int size;
    int out_size;
  } buffer;
};

namespace host_mqtt {

struct PublishedMessage {
  std::string topic;
  std::string payload;
  int qos;
  bool retain;
};

struct Subscription {
  std::string topic;
  int qos;
};

void reset(void);
const std::vector<PublishedMessage> &published(void);
const std::vector<Subscription> &subscriptions(void);
int startCount(void);
void deliver(const std::string &topic, const std::string &payload);
void deliverFragmented(const std::string &topic,
                       const std::string &payload,
                       int current_data_offset,
                       int total_data_len);
void brokerPublish(const std::string &topic, const std::string &payload, int qos, bool retain);

}  // namespace host_mqtt

extern "C" {
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client,
                                         mqtt_event_id_t event,
                                         esp_event_handler_t handler,
                                         void *handler_arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client,
                            const char *topic,
                            const char *data,
                            int len,
                            int qos,
                            int retain,
                            int store);
}

#endif
