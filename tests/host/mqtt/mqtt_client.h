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
  MQTT_EVENT_PUBLISHED,
};

constexpr int MQTT_PROTOCOL_V_3_1_1 = 4;

// esp_mqtt_error_type_t and esp_mqtt_connect_return_code_t from the pinned
// esp-mqtt header, in the values the component reports.
constexpr int MQTT_ERROR_TYPE_NONE = 0;
constexpr int MQTT_ERROR_TYPE_TCP_TRANSPORT = 1;
constexpr int MQTT_ERROR_TYPE_CONNECTION_REFUSED = 2;
constexpr int MQTT_ERROR_TYPE_SUBSCRIBE_FAILED = 3;

constexpr int MQTT_CONNECTION_ACCEPTED = 0;
constexpr int MQTT_CONNECTION_REFUSE_PROTOCOL = 1;
constexpr int MQTT_CONNECTION_REFUSE_ID_REJECTED = 2;
constexpr int MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE = 3;
constexpr int MQTT_CONNECTION_REFUSE_BAD_USERNAME = 4;
constexpr int MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED = 5;

struct esp_mqtt_error_codes_t {
  int error_type;
  int connect_return_code;
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
  int qos;
  bool retain;
  int msg_id;
  bool dup;
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
    int stack_size;
  } task;
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
  int message_id;
};

struct Subscription {
  std::string topic;
  int qos;
};

void reset(void);
const std::vector<PublishedMessage> &published(void);
const std::vector<Subscription> &subscriptions(void);
int startCount(void);
// Every esp_mqtt_client_start() call, including one that fails.
int startAttempts(void);
// The next start() call fails like esp_mqtt_client_start() on a refused socket
// or a failed client task.
void failNextStart(void);
// The next start() call reports a refused CONNACK with this return code, then
// the disconnect ESP-IDF follows it with, instead of connecting.
void refuseNextConnect(int connect_return_code);
bool hasRetained(const std::string &topic);
void deliver(const std::string &topic, const std::string &payload);
void deliverFragmented(const std::string &topic,
                       const std::string &payload,
                       int current_data_offset,
                       int total_data_len);
void deliverRetained(const std::string &topic, const std::string &payload);
void brokerPublish(const std::string &topic, const std::string &payload, int qos, bool retain);
void emitConnected(void);
void emitDisconnected(void);
void dropConnection(void);
void restoreConnection(void);
void emitPublished(int message_id);
void emitDataBurst(const std::string &topic, const std::string &payload, size_t count);
void emitRawDataBurst(const std::string &topic, const std::string &payload, size_t count);
void emitStaleData(const std::string &topic, const std::string &payload);
void emitStaleDisconnect(void);
// The next destroy delivers a command on the dying client and the next init
// reuses its address.
void emitDataOnDestroy(const std::string &topic, const std::string &payload);
int lastTaskStackSize(void);
int lastBufferSize(void);

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
