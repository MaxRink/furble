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
#include "FurbleUI.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "mqtt_host_dependencies.h"
#include "protocol/CameraListProtocol.h"

namespace {

int g_RawDataEvents = 0;

void rawHandler(void *, esp_event_base_t, int32_t, void *event_data) {
  const auto *event = static_cast<const esp_mqtt_event_t *>(event_data);
  if ((event != nullptr) && (event->event_id == MQTT_EVENT_DATA)) {
    g_RawDataEvents++;
  }
}

[[noreturn]] void fail(const std::string &message) {
  std::cerr << "mqtt_test: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

void ownerStep(Furble::MQTT &mqtt) {
  mqtt.hostTaskStep();
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
  using Furble::UI;

  Settings::reset();
  Control::reset();
  CameraList::reset();
  Device::setStringID("hub-42");
  host_mqtt::reset();
  host_mqtt_network::reset();
  host_mqtt_timer::reset();

  const std::string camera_address = "camera-7";
  auto camera = std::make_shared<Camera>(camera_address, "X100VI", Camera::Type::FUJIFILM_BASIC);
  camera->setRSSI(-61);
  CameraList::setCameras({camera});
  const uint8_t camera_stable_id = CameraList::getCameraId(camera.get());
  const std::string camera_id = std::to_string(camera_stable_id);
  UI::reset();

  auto &mqtt = MQTT::getInstance();
  MQTT::init();

  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0,
          "MQTT client started before a generic GOT_IP netif became ready");

  // This is deliberately an Ethernet-labelled netif. The production code must
  // follow the generic IP assignment, not a WiFi-only event or netif symbol.
  host_mqtt_network::setGotIp("ethernet", 0x0a00002a);
  mqtt.hostTaskStep();
  // startClient queues MQTT_EVENT_CONNECTED; the owner handles it on its next
  // step rather than running broker work on the callback stack.
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

  const auto online = publishedTopic(root_topic + "/status");
  require(online.payload == "online" && online.qos == 1 && online.retain,
          "online status publication is wrong");
  assertDiscoveryPayloads(root_topic, camera_id);

  const size_t filtered_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/other", "press");
  ownerStep(mqtt);
  require(Control::commands().size() == filtered_command_count,
          "unsubscribed MQTT topic reached command routing");

  host_mqtt::deliverFragmented(root_topic + "/cmd/shutter", "press", 0, 10);
  ownerStep(mqtt);
  require(Control::commands().size() == filtered_command_count,
          "fragmented MQTT command reached command routing");

  const size_t before_retained_replay = discoveryCount();
  host_mqtt::brokerPublish("homeassistant/status", "online", 0, true);
  ownerStep(mqtt);
  const size_t after_live_status = discoveryCount();
  require(after_live_status == before_retained_replay + 2,
          "retained Home Assistant status did not trigger discovery");

  host_mqtt::dropConnection();
  ownerStep(mqtt);
  require(!mqtt.isConnected(), "broker link loss did not clear connected state");
  require(host_mqtt::subscriptions().empty(),
          "clean-session broker retained subscriptions after link loss");
  host_mqtt::restoreConnection();
  ownerStep(mqtt);
  require(mqtt.isConnected(), "broker reconnect event did not restore connected state");
  require(host_mqtt::subscriptions().size() == 2,
          "connected event did not recreate clean-session subscriptions");
  const size_t after_reconnect_status = discoveryCount();
  require(after_reconnect_status == after_live_status + 4,
          "retained Home Assistant status was not replayed on reconnect");

  require(host_mqtt::hasRetained("homeassistant/device/furble_hub-42/config"),
          "hub discovery was not retained");
  require(
      host_mqtt::hasRetained("homeassistant/device/furble_hub-42_" + camera_id + "/config"),
      "camera discovery was not retained");
  mqtt.clearDiscovery();
  ownerStep(mqtt);
  require(!host_mqtt::hasRetained("homeassistant/device/furble_hub-42/config"),
          "empty retained hub discovery did not delete the broker record");
  require(
      !host_mqtt::hasRetained("homeassistant/device/furble_hub-42_" + camera_id + "/config"),
      "empty retained camera discovery did not delete the broker record");

  host_mqtt::deliver(root_topic + "/cmd/connect", camera_id);
  ownerStep(mqtt);
  require(UI::requests().size() == 1, "camera connect did not queue one UI request");
  require(UI::requests().back().request == UI::Request::CONNECT_SAVED
              && UI::requests().back().arg == camera_stable_id,
          "camera connect queued the wrong stable camera ID");

  host_mqtt::deliver(root_topic + "/cmd/disconnect", "ignored");
  ownerStep(mqtt);
  require(UI::requests().size() == 2, "camera disconnect did not queue one UI request");
  require(
      UI::requests().back().request == UI::Request::DISCONNECT && UI::requests().back().arg == 0,
      "camera disconnect queued the wrong UI request");

  host_mqtt::emitStaleDisconnect();
  ownerStep(mqtt);
  require(mqtt.isConnected(), "stale client callback changed the live connection state");

  const size_t unowned_release_count = Control::commands().size();
  Control::failNextRelease();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "release");
  ownerStep(mqtt);
  require(Control::commands().size() == unowned_release_count,
          "failed unowned shutter release unexpectedly changed command state");
  Control::failNextRelease();
  host_mqtt::deliver(root_topic + "/cmd/focus", "release");
  ownerStep(mqtt);
  require(Control::commands().size() == unowned_release_count,
          "failed unowned focus release unexpectedly changed command state");
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  host_mqtt::deliver(root_topic + "/cmd/focus", "press");
  ownerStep(mqtt);
  require(Control::commands().size() == unowned_release_count + 2
              && Control::commands()[unowned_release_count] == Control::CMD_SHUTTER_PRESS
              && Control::commands()[unowned_release_count + 1] == Control::CMD_FOCUS_PRESS,
          "failed unowned releases blocked later shutter or focus presses");
  host_mqtt::deliver(root_topic + "/cmd/shutter", "release");
  ownerStep(mqtt);
  host_mqtt::deliver(root_topic + "/cmd/focus", "release");
  ownerStep(mqtt);

  const size_t owned_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  host_mqtt::deliver(root_topic + "/cmd/focus", "press");
  ownerStep(mqtt);
  Control::failNextRelease();
  host_mqtt::emitDisconnected();
  ownerStep(mqtt);
  require(Control::commands().size() == owned_command_count + 4,
          "broker loss did not release both MQTT-owned actuators");
  require(Control::commands()[owned_command_count + 2] == Control::CMD_FOCUS_RELEASE,
          "broker loss did not release the MQTT-owned focus press");
  require(Control::commands()[owned_command_count + 3] == Control::CMD_SHUTTER_RELEASE,
          "broker-loss shutter release retry routed to the wrong action");
  ownerStep(mqtt);
  require(Control::commands().size() == owned_command_count + 4,
          "broker loss cleanup emitted a duplicate actuator release");

  mqtt.reloadSetting();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.isConnected(), "MQTT owner did not reconnect after broker-loss cleanup");

  const size_t command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", " press ");
  ownerStep(mqtt);
  require(Control::commands().size() == command_count + 1,
          "inbound shutter press did not reach Control");
  require(Control::commands().back() == Control::CMD_SHUTTER_PRESS,
          "inbound shutter press routed to the wrong action");
  require(publishedTopic(root_topic + "/state/shutter").payload == "held",
          "shutter press did not publish held state");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "release\n");
  ownerStep(mqtt);
  require(Control::commands().size() == command_count + 2,
          "inbound shutter release did not reach Control");
  require(Control::commands().back() == Control::CMD_SHUTTER_RELEASE,
          "inbound shutter release routed to the wrong action");
  require(latestPublishedTopic(root_topic + "/state/shutter").payload == "idle",
          "shutter release did not publish idle state");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "");
  ownerStep(mqtt);
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "unknown shutter command",
      "empty shutter command was not rejected");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "hold 60001");
  ownerStep(mqtt);
  require(latestPublishedTopic(root_topic + "/state/error").payload
              == "shutter hold must be 0-60000 ms",
          "oversized hold command was not rejected");

  host_mqtt::deliver(root_topic + "/cmd/location", "not-json");
  ownerStep(mqtt);
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "location payload is not JSON",
      "malformed location payload was not rejected");

  Control::setState(Control::STATE_IDLE);
  const size_t gated_command_count = Control::commands().size();
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  require(Control::commands().size() == gated_command_count,
          "command reached Control while it was not active");
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "shutter press rejected",
      "inactive Control state was not rejected");
  Control::setState(Control::STATE_ACTIVE);

  const size_t retained_command_count = Control::commands().size();
  host_mqtt::deliverRetained(root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  require(Control::commands().size() == retained_command_count,
          "retained shutter command reached Control");
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "retained MQTT command rejected",
      "retained shutter command did not publish a rejection");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "hold 5");
  ownerStep(mqtt);
  host_mqtt::emitDataBurst(root_topic + "/cmd/noise", "ignored", 4);
  const uint32_t notifications_before_hold_timer = host_mqtt_timer::notifications();
  host_mqtt_timer::fireAll();
  require(host_mqtt_timer::notifications() > notifications_before_hold_timer,
          "hold timer did not notify the MQTT owner task");
  host_mqtt_timer::advance(10);
  Control::failNextRelease();
  ownerStep(mqtt);
  require(Control::commands().size() == retained_command_count + 1,
          "failed hold release did not preserve the release intent");
  require(Control::commands()[retained_command_count] == Control::CMD_SHUTTER_PRESS,
          "hold command did not press the shutter");

  // Keep the release queue unavailable for this whole owner iteration. A new
  // press must remain gated until the pending release succeeds.
  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  Control::failNextRelease();
  ownerStep(mqtt);
  require(Control::commands().size() == retained_command_count + 1,
          "shutter press bypassed a pending hold release");
  ownerStep(mqtt);
  require(Control::commands().size() == retained_command_count + 2,
          "pending hold release was not retried while connected");
  require(Control::commands()[retained_command_count + 1] == Control::CMD_SHUTTER_RELEASE,
          "pending hold release routed to the wrong action");

  host_mqtt::deliver(root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  require(Control::commands().size() == retained_command_count + 3,
          "shutter press was not accepted after pending release completed");
  host_mqtt::deliver(root_topic + "/cmd/shutter", "release");
  ownerStep(mqtt);

  host_mqtt::deliver(root_topic + "/cmd/interval", "start");
  ownerStep(mqtt);
  host_mqtt_timer::advance(10);
  ownerStep(mqtt);
  const size_t interval_command_count = Control::commands().size();
  Control::failNextRelease();
  host_mqtt::deliver(root_topic + "/cmd/interval", "stop");
  ownerStep(mqtt);
  require(Control::commands().size() == interval_command_count,
          "failed interval release did not preserve the release intent");
  host_mqtt::deliver(root_topic + "/cmd/interval", "start");
  Control::failNextRelease();
  ownerStep(mqtt);
  require(Control::commands().size() == interval_command_count,
          "interval start bypassed a pending shutter release");
  ownerStep(mqtt);
  require(Control::commands().size() == interval_command_count + 1,
          "pending interval release was not retried while connected");
  require(Control::commands().back() == Control::CMD_SHUTTER_RELEASE,
          "pending interval release routed to the wrong action");
  host_mqtt::deliver(root_topic + "/cmd/interval", "start");
  ownerStep(mqtt);

  const size_t beforeReplacement = Control::commands().size();
  host_mqtt::emitStaleData(root_topic + "/cmd/shutter", "press");
  Settings::mqttBase = "replacement";
  mqtt.reloadSetting();
  ownerStep(mqtt);
  require(Control::commands().size() == beforeReplacement,
          "queued actuator event from the stopped client was executed");
  require(std::none_of(Control::commands().begin() + beforeReplacement, Control::commands().end(),
                       [](const auto command) { return command == Control::CMD_SHUTTER_PRESS; }),
          "queued actuator event from the stopped client pressed the shutter");
  const auto offlineBeforeReplacement = latestPublishedTopic(root_topic + "/status");
  require(offlineBeforeReplacement.payload == "offline" && mqtt.isConnected(),
          "clean stop did not wait for the offline publication acknowledgement");
  host_mqtt::emitPublished(offlineBeforeReplacement.message_id);
  ownerStep(mqtt);
  require(host_mqtt::startCount() == 3,
          "reload did not create a replacement MQTT client after offline acknowledgement");

  // Reconnect after draining the old client's queue. Fill the payload queue
  // before delivering CONNECTED to prove lifecycle state cannot be stranded by
  // callback-queue saturation.
  const std::string replacement_root_topic = "replacement/hub-42";
  // Deliberately bypass broker receipt to saturate the callback payload queue
  // before CONNECTED subscribes the replacement client.
  host_mqtt::emitRawDataBurst(replacement_root_topic + "/cmd/noise", "ignored", 4);
  host_mqtt::emitConnected();
  ownerStep(mqtt);
  require(mqtt.isConnected(), "connected lifecycle event was lost when payload queue was full");
  require(host_mqtt::startCount() == 3, "reload did not create a replacement MQTT client");
  require(publishedTopic(replacement_root_topic + "/status").payload == "online",
          "replacement client did not publish on the reloaded topic");

  Settings::mqttBase = "replacement-timeout";
  mqtt.reloadSetting();
  ownerStep(mqtt);
  const std::string timeout_root_topic = "replacement-timeout/hub-42";
  const auto offlineOnReloadTimeout = latestPublishedTopic(replacement_root_topic + "/status");
  require(offlineOnReloadTimeout.payload == "offline" && mqtt.isConnected(),
          "reload timeout did not wait for the old client's offline acknowledgement");
  host_mqtt_timer::advance(1000);
  ownerStep(mqtt);
  require(host_mqtt::startCount() == 4,
          "reload timeout did not destroy the old client and create a replacement");
  ownerStep(mqtt);
  require(publishedTopic(timeout_root_topic + "/status").payload == "online",
          "timeout replacement did not publish on its reloaded topic");

  const size_t beforeManualStop = Control::commands().size();
  host_mqtt::deliver(timeout_root_topic + "/cmd/shutter", "press");
  ownerStep(mqtt);
  host_mqtt::deliver(timeout_root_topic + "/cmd/focus", "press");
  ownerStep(mqtt);
  mqtt.disconnect();
  ownerStep(mqtt);
  require(Control::commands().size() == beforeManualStop + 4,
          "manual stop did not release MQTT-owned shutter and focus presses");
  require(Control::commands()[beforeManualStop + 2] == Control::CMD_SHUTTER_RELEASE,
          "manual stop did not release the MQTT-owned shutter press");
  require(Control::commands()[beforeManualStop + 3] == Control::CMD_FOCUS_RELEASE,
          "manual stop did not release the MQTT-owned focus press");
  const auto offline = latestPublishedTopic(timeout_root_topic + "/status");
  require(offline.payload == "offline" && offline.retain,
          "clean disconnect did not enqueue retained offline status before timeout");
  host_mqtt_timer::advance(1000);
  ownerStep(mqtt);
  require(!mqtt.isConnected(), "MQTT disconnect did not clear connected state");

  host_mqtt::reset();
  esp_mqtt_client_config_t raw_config = {};
  auto *raw_client = esp_mqtt_client_init(&raw_config);
  require(raw_client != nullptr, "raw MQTT fixture did not initialize");
  require(esp_mqtt_client_register_event(raw_client, MQTT_EVENT_ANY, rawHandler, nullptr) == ESP_OK,
          "raw MQTT fixture did not register");
  require(esp_mqtt_client_start(raw_client) == ESP_OK, "raw MQTT fixture did not start");
  require(esp_mqtt_client_init(&raw_config) == nullptr,
          "duplicate MQTT init overwrote the active client");
  require(esp_mqtt_client_subscribe(raw_client, "old/#", 1) > 0,
          "raw MQTT fixture did not subscribe");
  require(!host_mqtt::subscriptions().empty(), "raw MQTT fixture lost its subscription");
  require(esp_mqtt_client_destroy(raw_client) == ESP_OK, "raw MQTT fixture did not destroy");
  require(host_mqtt::subscriptions().empty(),
          "clean-session subscriptions survived client destruction");

  host_mqtt::reset();
  g_RawDataEvents = 0;
  raw_client = esp_mqtt_client_init(&raw_config);
  require(raw_client != nullptr, "reinitialized raw MQTT fixture did not initialize");
  require(esp_mqtt_client_register_event(raw_client, MQTT_EVENT_ANY, rawHandler, nullptr) == ESP_OK,
          "reinitialized raw MQTT fixture did not register");
  require(esp_mqtt_client_start(raw_client) == ESP_OK,
          "reinitialized raw MQTT fixture did not start");
  host_mqtt::brokerPublish("old/camera/state", "stale", 0, false);
  require(g_RawDataEvents == 0, "subscriptions leaked into the reinitialized MQTT fixture");
  g_RawDataEvents = 0;
  require(esp_mqtt_client_subscribe(raw_client, "old/#", 1) > 0,
          "reinitialized raw MQTT fixture did not subscribe");
  host_mqtt::brokerPublish("old/camera/state", "visible", 0, false);
  require(g_RawDataEvents == 1, "matching wildcard subscription did not receive a publication");
  host_mqtt::brokerPublish("other/camera/state", "hidden", 0, false);
  require(g_RawDataEvents == 1, "unmatched wildcard subscription received a publication");
  require(esp_mqtt_client_subscribe(raw_client, "bad/#/filter", 1) < 0,
          "invalid MQTT # filter was accepted");
  require(esp_mqtt_client_subscribe(raw_client, "bad+filter", 1) < 0,
          "invalid MQTT + filter was accepted");
  require(esp_mqtt_client_subscribe(raw_client, "#", 1) > 0, "valid MQTT # filter was rejected");
  host_mqtt::brokerPublish("$SYS/status", "hidden", 0, false);
  require(g_RawDataEvents == 1, "wildcard subscription matched an MQTT $ topic");
  host_mqtt::brokerPublish("app/status", "visible", 0, false);
  require(g_RawDataEvents == 2, "valid MQTT # filter did not match an app topic");
  require(esp_mqtt_client_destroy(raw_client) == ESP_OK, "raw MQTT fixture did not destroy");
  require(host_mqtt::subscriptions().empty(),
          "clean-session subscriptions survived client destruction");

  std::cout << "mqtt host loopback: PASS\n";
  return EXIT_SUCCESS;
}
