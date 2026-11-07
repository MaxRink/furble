#include "FurbleMQTT.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CameraList.h"
#include "Device.h"
#include "FurbleMQTTMemory.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
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
  host_mqtt_heap::reset();

  // The host clock is virtual. A real time step between owner iterations used
  // to expire an offline-ACK deadline and make this suite flaky.
  const int64_t clockBefore = esp_timer_get_time();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  require(esp_timer_get_time() == clockBefore, "the host MQTT clock advanced with wall time");

  // The budget constants are part of the firmware contract. Keep the host test
  // pinned to the same values the device compiles.
  require(Furble::MQTTMemory::OWNER_STACK_BYTES == 10240, "MQTT owner stack budget changed");
  require(Furble::MQTTMemory::PLAIN_WORKER_STACK_BYTES == 3072,
          "plain MQTT worker stack budget changed");
  require(Furble::MQTTMemory::TLS_WORKER_STACK_BYTES == 6144,
          "TLS MQTT worker stack budget changed");
  require(Furble::MQTTMemory::BUFFER_SIZE == 1536, "MQTT buffer budget changed");
  require(Furble::MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR
              >= Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR
                     + (Furble::MQTTMemory::TLS_WORKER_STACK_BYTES
                        - Furble::MQTTMemory::PLAIN_WORKER_STACK_BYTES),
          "the TLS start floor does not cover the larger worker");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR - 1,
                                         Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR),
          "start gate opened below the free floor");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR,
                                         Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR - 1),
          "start gate opened below the largest-block floor");
  require(
      !Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR - 1,
                                     Furble::MQTTMemory::START_TLS_INTERNAL_LARGEST_FLOOR, true),
      "TLS start gate opened below the free floor");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR,
                                         Furble::MQTTMemory::START_TLS_INTERNAL_LARGEST_FLOOR - 1,
                                         true),
          "TLS start gate opened below the largest-block floor");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR,
                                         Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR, true),
          "the TLS start gate reused the smaller plain-worker floor");
  require(Furble::MQTTMemory::EVENT_QUEUE_BYTES == 9392,
          "MQTT event queue budget changed without updating the gate");
  require(Furble::MQTTMemory::EVENT_STORAGE_BYTES
              == Furble::MQTTMemory::EVENT_QUEUE_BYTES + Furble::MQTTMemory::INCOMING_EVENT_BYTES,
          "the internal event reserve must cover the queue and the incoming copy");
  require(Furble::MQTTMemory::INCOMING_EVENT_BYTES * 4 == Furble::MQTTMemory::EVENT_QUEUE_BYTES,
          "the incoming copy is one of the four queue items");
  require(
      Furble::MQTTMemory::startFits(
          Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR + Furble::MQTTMemory::EVENT_STORAGE_BYTES,
          Furble::MQTTMemory::EVENT_QUEUE_BYTES, false, Furble::MQTTMemory::EVENT_STORAGE_BYTES,
          Furble::MQTTMemory::EVENT_QUEUE_BYTES),
      "the queue and the incoming copy are separate blocks, not one");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR
                                             + Furble::MQTTMemory::EVENT_STORAGE_BYTES - 1,
                                         Furble::MQTTMemory::EVENT_QUEUE_BYTES, false,
                                         Furble::MQTTMemory::EVENT_STORAGE_BYTES,
                                         Furble::MQTTMemory::EVENT_QUEUE_BYTES),
          "start gate ignored the incoming copy in the free floor");
  require(
      !Furble::MQTTMemory::startFits(
          Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR + Furble::MQTTMemory::EVENT_QUEUE_BYTES - 1,
          Furble::MQTTMemory::EVENT_QUEUE_BYTES, false, Furble::MQTTMemory::EVENT_QUEUE_BYTES),
      "start gate ignored the internal queue free floor");
  require(Furble::MQTTMemory::startFits(
              Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR + Furble::MQTTMemory::EVENT_QUEUE_BYTES,
              Furble::MQTTMemory::EVENT_QUEUE_BYTES, false, Furble::MQTTMemory::EVENT_QUEUE_BYTES),
          "start gate closed at the internal queue floor");
  require(!Furble::MQTTMemory::startFits(65536, Furble::MQTTMemory::EVENT_QUEUE_BYTES - 1, false,
                                         Furble::MQTTMemory::EVENT_QUEUE_BYTES),
          "start gate ignored the internal queue largest-block floor");
  require(!Furble::MQTTMemory::ownerFits(Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR - 1, 65536),
          "owner gate opened below the free floor");
  require(!Furble::MQTTMemory::ownerFits(Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR,
                                         Furble::MQTTMemory::OWNER_INTERNAL_LARGEST_FLOOR - 1),
          "owner gate opened below the largest-block floor");
  require(Furble::MQTTMemory::ownerFits(Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR,
                                        Furble::MQTTMemory::OWNER_INTERNAL_LARGEST_FLOOR),
          "owner gate closed at its exact floors");
  require(!Furble::MQTTMemory::discoveryFits(Furble::MQTTMemory::DISCOVERY_INTERNAL_FREE_FLOOR - 1,
                                             Furble::MQTTMemory::DISCOVERY_INTERNAL_LARGEST_FLOOR),
          "discovery gate opened below the free floor");
  require(
      !Furble::MQTTMemory::discoveryFits(Furble::MQTTMemory::DISCOVERY_INTERNAL_FREE_FLOOR,
                                         Furble::MQTTMemory::DISCOVERY_INTERNAL_LARGEST_FLOOR - 1),
      "discovery gate opened below the largest-block floor");

  const std::string camera_address = "camera-7";
  auto camera = std::make_shared<Camera>(camera_address, "X100VI", Camera::Type::FUJIFILM_BASIC);
  camera->setRSSI(-61);
  CameraList::setCameras({camera});
  const uint8_t camera_stable_id = CameraList::getCameraId(camera.get());
  const std::string camera_id = std::to_string(camera_stable_id);
  UI::reset();

  auto &mqtt = MQTT::getInstance();

  // The owner is 10 KiB. Do not create it when the pool cannot also hold the
  // client and buffers. A later reload with recovered heap creates it.
  host_mqtt_heap::set(Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR - 1, 65536);
  MQTT::init();
  require(!mqtt.hostTaskCreated(), "MQTT owner was created below the pre-task floor");
  host_mqtt_heap::reset();
  mqtt.reloadSetting();
  require(mqtt.hostTaskCreated(), "MQTT owner was not created after the heap recovered");

  // On a board without PSRAM the 9,392-byte queue is internal. The start gate
  // must include it, otherwise a client is admitted with only the worker floor.
  host_mqtt_heap::setPsram(0, 0);
  host_mqtt_heap::set(
      Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR + Furble::MQTTMemory::EVENT_QUEUE_BYTES - 1,
      65536);
  mqtt.reloadSetting();
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0,
          "start gate admitted a client whose internal queue cannot fit");
  host_mqtt_heap::reset();
  mqtt.reloadSetting();

  // P-030: the owner reads its five settings on reloadSetting(), not on every
  // iteration, and idles on a one-second deadline instead of four times a
  // second. A changed setting without a reload must not alter the cached
  // answer; an explicit reload must.
  require(mqtt.isConfigured(), "valid cached settings are configured");
  Settings::mqttURI.clear();
  require(mqtt.isConfigured(), "the owner does not re-read NVS between reloads");
  mqtt.reloadSetting();
  require(!mqtt.isConfigured(), "reloadSetting picks up an invalid URI");
  Settings::mqttURI = "mqtt://loopback";
  mqtt.reloadSetting();
  require(mqtt.isConfigured(), "reloadSetting restores a valid URI");
  require(mqtt.hostTaskWaitMs() == 1000, "the idle owner wait is one second");

  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0,
          "MQTT client started before a generic GOT_IP netif became ready");

  // This is deliberately an Ethernet-labelled netif. The production code must
  // follow the generic IP assignment, not a WiFi-only event or netif symbol.
  host_mqtt_network::setGotIp("ethernet", 0x0a00002a);

  // The start gate must refuse a pool that cannot hold the esp-mqtt worker and
  // buffers. It reports a failed attempt instead of letting esp-mqtt allocate
  // into the last few hundred bytes.
  host_mqtt_heap::set(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR - 1, 65536);
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0, "start gate allowed a low-heap MQTT client");
  require(!mqtt.isConnected(), "a deferred MQTT start reported connected");

  host_mqtt_heap::reset();
  mqtt.reloadSetting();
  mqtt.hostTaskStep();
  // startClient queues MQTT_EVENT_CONNECTED; the owner handles it on its next
  // step rather than running broker work on the callback stack.
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 1, "MQTT client did not start after Ethernet GOT_IP");
  require(host_mqtt::lastTaskStackSize()
              == static_cast<int>(Furble::MQTTMemory::PLAIN_WORKER_STACK_BYTES),
          "plain MQTT worker did not use the budgeted stack");
  require(host_mqtt::lastBufferSize() == static_cast<int>(Furble::MQTTMemory::BUFFER_SIZE),
          "MQTT network buffer did not use the budgeted size");
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

  // The owner caches MQTT_HA in reloadSetting(). A settings change between
  // reloads must not be observed, and no owner iteration may read the key.
  const uint32_t haLoads = Settings::mqttHALoads;
  Settings::mqttHA = false;
  ownerStep(mqtt);
  host_mqtt::brokerPublish("homeassistant/status", "online", 0, true);
  ownerStep(mqtt);
  require(Settings::mqttHALoads == haLoads, "MQTT owner read MQTT_HA outside reloadSetting");
  require(discoveryCount() == 4, "the cached MQTT_HA value was not used");
  Settings::mqttHA = true;

  // Discovery is optional. Below the gate the broker link must stay up and no
  // retained discovery record may be published. The Home Assistant status
  // replay is the simplest path that reaches publishDiscovery().
  const size_t beforeGate = discoveryCount();
  host_mqtt_heap::set(Furble::MQTTMemory::DISCOVERY_INTERNAL_FREE_FLOOR - 1, 65536);
  host_mqtt::brokerPublish("homeassistant/status", "online", 0, true);
  ownerStep(mqtt);
  require(discoveryCount() == beforeGate, "HA discovery ran below the memory floor");
  require(mqtt.isConnected(), "the discovery gate dropped the MQTT session");
  host_mqtt_heap::reset();

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
  require(host_mqtt::hasRetained("homeassistant/device/furble_hub-42_" + camera_id + "/config"),
          "camera discovery was not retained");
  mqtt.clearDiscovery();
  ownerStep(mqtt);
  require(!host_mqtt::hasRetained("homeassistant/device/furble_hub-42/config"),
          "empty retained hub discovery did not delete the broker record");
  require(!host_mqtt::hasRetained("homeassistant/device/furble_hub-42_" + camera_id + "/config"),
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
  require(latestPublishedTopic(root_topic + "/state/error").payload == "unknown shutter command",
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
  require(latestPublishedTopic(root_topic + "/state/error").payload == "shutter press rejected",
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
  require(mqtt.hostTaskWaitMs() == 250, "a pending release did not shorten the owner wait");

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

  // Secure URI fixture: mqtts:// must select the 6 KiB TLS worker and must not
  // start on a pool that only satisfies the plain 3 KiB worker floor.
  const size_t beforeSecure = host_mqtt::startCount();
  Settings::mqttURI = "mqtts://broker.example:8883";
  mqtt.reloadSetting();
  host_mqtt_heap::set(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR, 65536);
  ownerStep(mqtt);
  require(static_cast<size_t>(host_mqtt::startCount()) == beforeSecure,
          "secure MQTT started below the TLS worker floor");
  host_mqtt_heap::reset();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(static_cast<size_t>(host_mqtt::startCount()) == beforeSecure + 1,
          "secure MQTT did not start above the TLS worker floor");
  require(host_mqtt::lastTaskStackSize()
              == static_cast<int>(Furble::MQTTMemory::TLS_WORKER_STACK_BYTES),
          "secure MQTT worker did not use the 6 KiB TLS stack");
  require(mqtt.isConnected(), "secure MQTT fixture did not connect");
  mqtt.disconnect();
  ownerStep(mqtt);
  host_mqtt_timer::advance(1000);
  ownerStep(mqtt);
  require(!mqtt.isConnected(), "secure MQTT fixture did not stop");

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
