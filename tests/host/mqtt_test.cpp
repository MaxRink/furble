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

[[noreturn]] void fail(const std::string &message) {
  std::cerr << "mqtt_test: " << message << '\n';
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

  const auto &messages = host_mqtt::published();
  const size_t discovery_count =
      static_cast<size_t>(std::count_if(messages.begin(), messages.end(), [](const auto &message) {
        return message.topic.rfind("homeassistant/device/", 0) == 0;
      }));
  require(discovery_count == 2, "expected one hub and one camera discovery publication");

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
  require(online.payload == "online" && online.retain, "online status publication is wrong");
  assertDiscoveryPayloads(root_topic, camera_id);

  const size_t command_count = Control::commands().size();
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

  std::cout << "mqtt host loopback: PASS\n";
  return EXIT_SUCCESS;
}
