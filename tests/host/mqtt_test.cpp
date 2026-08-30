#include "FurbleMQTT.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "CameraList.h"
#include "Device.h"
#include "FurbleSettings.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "mqtt_host_dependencies.h"

namespace {

int g_RawDataEvents = 0;

void rawHandler(void *, esp_event_base_t, int32_t, void *event_data) {
  const auto *event = static_cast<const esp_mqtt_event_t *>(event_data);
  if ((event != nullptr) && (event->event_id == MQTT_EVENT_DATA)) {
    g_RawDataEvents++;
  }
}

[[noreturn]] void fail(const std::string &message) {
  std::cerr << "mqtt_host_broker_test: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

const host_mqtt::PublishedMessage &publishedTopic(const std::string &topic) {
  const auto &messages = host_mqtt::published();
  const auto found = std::find_if(messages.begin(), messages.end(),
                                  [&](const auto &message) { return message.topic == topic; });
  require(found != messages.end(), "missing published topic " + topic);
  return *found;
}

const host_mqtt::PublishedMessage &latestPublishedTopic(const std::string &topic) {
  const auto &messages = host_mqtt::published();
  const auto found = std::find_if(messages.rbegin(), messages.rend(),
                                  [&](const auto &message) { return message.topic == topic; });
  require(found != messages.rend(), "missing published topic " + topic);
  return *found;
}

size_t discoveryCount(void) {
  const auto &messages = host_mqtt::published();
  return static_cast<size_t>(std::count_if(
      messages.begin(), messages.end(),
      [](const auto &message) { return message.topic.rfind("homeassistant/device/", 0) == 0; }));
}

const cJSON *objectItem(const cJSON *object, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  require(item != nullptr, std::string("missing JSON member ") + name);
  return item;
}

void requireString(const cJSON *object, const char *name, const std::string &expected) {
  const cJSON *item = objectItem(object, name);
  require(item->type == cJSON_String, std::string("JSON member is not a string ") + name);
  require(item->valuestring != nullptr && item->valuestring == expected,
          std::string("unexpected JSON member ") + name);
}

const cJSON *parsePayload(const std::string &payload) {
  cJSON *root = cJSON_ParseWithLength(payload.c_str(), payload.size());
  require(root != nullptr, "published payload is not valid JSON");
  return root;
}

void assertDiscoveryPayloads(const std::string &root_topic, const std::string &camera_id) {
  const std::string hub_topic = "homeassistant/device/furble_hub-42/config";
  const std::string camera_topic = "homeassistant/device/furble_hub-42_" + camera_id + "/config";

  require(discoveryCount() == 2, "expected one hub and one camera discovery publication");

  const auto &hub_message = publishedTopic(hub_topic);
  require(hub_message.retain, "hub discovery must be retained");
  const cJSON *hub = parsePayload(hub_message.payload);
  objectItem(hub, "dev");
  objectItem(hub, "o");
  const cJSON *hub_components = objectItem(hub, "cmps");
  const cJSON *shutter = objectItem(hub_components, "shutter");
  requireString(shutter, "p", "button");
  requireString(shutter, "command_topic", root_topic + "/cmd/shutter");
  requireString(shutter, "payload_press", "hold 200");
  const cJSON *interval = objectItem(hub_components, "interval");
  requireString(interval, "p", "switch");
  requireString(interval, "command_topic", root_topic + "/cmd/interval");
  requireString(interval, "state_topic", root_topic + "/state/interval");
  cJSON_Delete(const_cast<cJSON *>(hub));

  const auto &camera_message = publishedTopic(camera_topic);
  require(camera_message.retain, "camera discovery must be retained");
  const cJSON *camera = parsePayload(camera_message.payload);
  objectItem(camera, "dev");
  objectItem(camera, "o");
  const cJSON *camera_components = objectItem(camera, "cmps");
  const cJSON *connect = objectItem(camera_components, "connect");
  requireString(connect, "p", "button");
  requireString(connect, "command_topic", root_topic + "/cmd/connect");
  requireString(connect, "payload_press", camera_id);
  cJSON_Delete(const_cast<cJSON *>(camera));
}

}  // namespace

int main(void) {
  using Furble::Camera;
  using Furble::CameraList;
  using Furble::Control;
  using Furble::Device;
  using Furble::MQTT;
  using Furble::Settings;

  Settings::reset();
  Control::reset();
  CameraList::reset();
  Device::setStringID("hub-42");
  host_mqtt::reset();
  host_mqtt_network::reset();

  const std::string camera_id = "camera-7";
  auto camera = std::make_shared<Camera>(camera_id, "X100VI", Camera::Type::FUJIFILM_BASIC);
  camera->setRSSI(-61);
  CameraList::setCameras({camera});

  auto &mqtt = MQTT::getInstance();
  MQTT::init();

  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0,
          "MQTT client started before a generic GOT_IP netif became ready");

  // This is deliberately an Ethernet-labelled netif. The production code must
  // follow the generic IP assignment, not a WiFi-only event or netif symbol.
  host_mqtt_network::setGotIp("ethernet", 0x0a00002a);
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 1, "MQTT client did not start after Ethernet GOT_IP");
  require(mqtt.isConnected(), "MQTT client did not process the connected event");

  const std::string root_topic = "furble/hub-42";
  const auto &subscriptions = host_mqtt::subscriptions();
  require(std::any_of(subscriptions.begin(), subscriptions.end(),
                      [&](const auto &subscription) {
                        return subscription.topic == root_topic + "/cmd/#" && subscription.qos == 1;
                      }),
          "device command wildcard was not subscribed");
  require(std::any_of(subscriptions.begin(), subscriptions.end(),
                      [](const auto &subscription) {
                        return subscription.topic == "homeassistant/status"
                               && subscription.qos == 0;
                      }),
          "Home Assistant status was not subscribed");

  const auto &online = publishedTopic(root_topic + "/status");
  require(online.payload == "online" && online.qos == 1 && online.retain,
          "online status publication is wrong");
  assertDiscoveryPayloads(root_topic, camera_id);

  const size_t command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/other", "press");
  require(Control::commands().size() == command_count,
          "unsubscribed MQTT topic reached command routing");

  host_mqtt::deliverFragmented(root_topic + "/cmd/shutter", "press", 0, 10);
  require(Control::commands().size() == command_count,
          "fragmented MQTT command reached command routing");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "");
  require(latestPublishedTopic(root_topic + "/state/error").payload == "unknown shutter command",
          "empty shutter command was not rejected");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "hold 60001");
  require(latestPublishedTopic(root_topic + "/state/error").payload
              == "shutter hold must be 0-60000 ms",
          "oversized hold command was not rejected");

  host_mqtt::deliver(root_topic + "/cmd/location", "not-json");
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "location payload is not JSON",
      "malformed location payload was not rejected");

  host_mqtt::deliver(root_topic + "/cmd/shutter", " press ");
  require(Control::commands().size() == command_count + 1,
          "inbound shutter press did not reach Control");
  require(Control::commands().back() == Control::CMD_SHUTTER_PRESS,
          "inbound shutter press routed to the wrong action");
  require(publishedTopic(root_topic + "/state/shutter").payload == "held",
          "shutter press did not publish held state");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "release\n");
  require(Control::commands().size() == command_count + 2,
          "inbound shutter release did not reach Control");
  require(Control::commands().back() == Control::CMD_SHUTTER_RELEASE,
          "inbound shutter release routed to the wrong action");
  require(latestPublishedTopic(root_topic + "/state/shutter").payload == "idle",
          "shutter release did not publish idle state");

  Control::setState(Control::STATE_IDLE);
  const size_t gated_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  require(Control::commands().size() == gated_command_count,
          "command reached Control while it was not active");
  require(latestPublishedTopic(root_topic + "/state/error").payload == "shutter press rejected",
          "inactive Control state was not rejected");
  Control::setState(Control::STATE_ACTIVE);

  const size_t before_status = discoveryCount();
  host_mqtt::brokerPublish("homeassistant/status", "online", 0, true);
  require(discoveryCount() == before_status + 2,
          "retained Home Assistant status did not republish discovery");

  host_mqtt::dropConnection();
  require(!mqtt.isConnected(), "unexpected broker loss did not clear connected state");
  require(host_mqtt::subscriptions().empty(),
          "clean-session broker retained subscriptions after link loss");
  const size_t lost_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  require(Control::commands().size() == lost_command_count,
          "command reached MQTT after unexpected broker loss");
  host_mqtt::restoreConnection();
  require(mqtt.isConnected(), "broker reconnect event did not restore connected state");
  require(host_mqtt::subscriptions().size() == 2,
          "connected event did not recreate clean-session MQTT subscriptions");

  require(host_mqtt::hasRetained("homeassistant/device/furble_hub-42/config"),
          "hub discovery was not retained");
  require(host_mqtt::hasRetained("homeassistant/device/furble_hub-42_camera-7/config"),
          "camera discovery was not retained");
  mqtt.clearDiscovery();
  mqtt.hostTaskStep();
  require(!host_mqtt::hasRetained("homeassistant/device/furble_hub-42/config"),
          "empty retained hub discovery did not delete the broker record");
  require(!host_mqtt::hasRetained("homeassistant/device/furble_hub-42_camera-7/config"),
          "empty retained camera discovery did not delete the broker record");

  mqtt.disconnect();
  mqtt.hostTaskStep();
  require(!mqtt.isConnected(), "MQTT disconnect did not clear connected state");
  const size_t offline_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  require(Control::commands().size() == offline_command_count,
          "offline MQTT client accepted a command");

  mqtt.reloadSetting();
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 2 && mqtt.isConnected(),
          "MQTT client did not reconnect after reload");
  require(discoveryCount() >= before_status + 4,
          "retained Home Assistant status was not delivered on reconnect");

  mqtt.disconnect();
  mqtt.hostTaskStep();
  host_mqtt::reset();
  esp_mqtt_client_config_t config = {};
  auto *client = esp_mqtt_client_init(&config);
  require(client != nullptr, "first raw MQTT fixture did not initialize");
  require(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, rawHandler, nullptr) == ESP_OK,
          "first raw MQTT fixture did not register");
  require(esp_mqtt_client_start(client) == ESP_OK, "first raw MQTT fixture did not start");
  require(esp_mqtt_client_subscribe(client, "old/#", 1) > 0,
          "first raw MQTT fixture did not subscribe");
  require(esp_mqtt_client_init(&config) == nullptr,
          "duplicate MQTT init overwrote the active client");
  require(esp_mqtt_client_destroy(client) == ESP_OK, "first raw MQTT fixture did not destroy");
  host_mqtt::reset();
  client = esp_mqtt_client_init(&config);
  require(client != nullptr, "second raw MQTT fixture did not initialize");
  require(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, rawHandler, nullptr) == ESP_OK,
          "second raw MQTT fixture did not register");
  require(esp_mqtt_client_start(client) == ESP_OK, "second raw MQTT fixture did not start");
  host_mqtt::brokerPublish("old/topic", "stale", 1, false);
  require(g_RawDataEvents == 0, "subscriptions leaked from the destroyed MQTT fixture");
  require(esp_mqtt_client_subscribe(client, "bad/#/filter", 1) < 0,
          "invalid MQTT # filter was accepted");
  require(esp_mqtt_client_subscribe(client, "bad+filter", 1) < 0,
          "invalid MQTT + filter was accepted");
  require(esp_mqtt_client_subscribe(client, "#", 1) > 0, "valid MQTT # filter was rejected");
  host_mqtt::brokerPublish("$SYS/status", "hidden", 0, false);
  require(g_RawDataEvents == 0, "wildcard subscription matched an MQTT $ topic");
  host_mqtt::brokerPublish("app/status", "visible", 0, false);
  require(g_RawDataEvents == 1, "valid wildcard subscription did not match an app topic");
  require(esp_mqtt_client_destroy(client) == ESP_OK, "second raw MQTT fixture did not destroy");
  host_mqtt::reset();

  std::cout << "mqtt in-process broker: PASS\n";
  return EXIT_SUCCESS;
}
