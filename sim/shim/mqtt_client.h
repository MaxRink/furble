#ifndef FURBLE_SIM_MQTT_CLIENT_H
#define FURBLE_SIM_MQTT_CLIENT_H

#include <cstdint>

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
  int qos;
  bool retain;
  int msg_id;
  bool dup;
};

using esp_mqtt_event_handle_t = esp_mqtt_event_t *;

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
