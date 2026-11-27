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
#include "FurbleOTA.h"
#include "FurbleOTACommand.h"
#include "FurbleOTAHTTPS.h"
#include "FurbleOTAStatus.h"
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

/** Scripted HTTPS client: open, poll, verify and apply without a network. */
class OtaClient final: public Furble::OTA::HTTPS::Client {
 public:
  struct Step {
    bool failed = false;
    bool complete = false;
    size_t bytes = 0;
  };

  size_t imageSize = 1000;
  size_t slotCapacity = 4096;
  std::vector<Step> plan;
  Verify verifyResult = Verify::Ok;
  size_t openCalls = 0;
  size_t pollCalls = 0;
  size_t applyCalls = 0;
  size_t discardCalls = 0;

  bool open(const char *, size_t, Open &result) override {
    openCalls++;
    result.imageSize = imageSize;
    result.slotCapacity = slotCapacity;
    planIndex = 0;
    return true;
  }

  Poll poll() override {
    pollCalls++;
    Poll result;
    if (planIndex >= plan.size()) {
      result.failed = true;
      return result;
    }
    const Step step = plan[planIndex++];
    result.failed = step.failed;
    result.complete = step.complete;
    result.bytesRetrieved = step.bytes;
    return result;
  }

  Verify finish() override { return verifyResult; }
  bool apply() override {
    applyCalls++;
    return true;
  }
  void discard() override { discardCalls++; }

 private:
  size_t planIndex = 0;
};

class OtaSource final: public Furble::OTA::Command::Source {
 public:
  bool succeeds = true;
  std::string version = "1.3.0";
  size_t imageSize = 1000;
  size_t slotCapacity = 4096;
  size_t calls = 0;

  bool fetch(const char *, Available &available) override {
    calls++;
    if (!succeeds) {
      return false;
    }
    std::snprintf(available.version, sizeof(available.version), "%s", version.c_str());
    available.imageSize = imageSize;
    available.slotCapacity = slotCapacity;
    return true;
  }
};

class OtaDetail final: public Furble::OTA::Command::Detail {
 public:
  std::string value = "none";
  const char *detail(void) const override { return value.c_str(); }
};

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
              == Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES
                     + Furble::MQTTMemory::EVENT_COPIES_BLOCK_BYTES,
          "the internal event reserve must cover the queue and both event copies");
  require(Furble::MQTTMemory::EVENT_COPY_BYTES * 4 == Furble::MQTTMemory::EVENT_QUEUE_BYTES,
          "an event copy is one of the four queue items");
  require(Furble::MQTTMemory::EVENT_COPIES_BYTES == 2 * Furble::MQTTMemory::EVENT_COPY_BYTES,
          "the owner preallocates the incoming and the received copy");
  require(Furble::MQTTMemory::BLOCK_OVERHEAD_BYTES >= 84 + 16,
          "the block overhead covers StaticQueue_t plus the TLSF header and poisoning");
  require(!Furble::MQTTMemory::startFits(65536, Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES - 1,
                                         false, Furble::MQTTMemory::EVENT_STORAGE_BYTES,
                                         Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES),
          "the largest block must hold the queue with its control block");
  require(
      Furble::MQTTMemory::startFits(
          Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR + Furble::MQTTMemory::EVENT_STORAGE_BYTES,
          Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES, false,
          Furble::MQTTMemory::EVENT_STORAGE_BYTES, Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES),
      "the queue and the event copies are separate blocks, not one");
  require(!Furble::MQTTMemory::startFits(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR
                                             + Furble::MQTTMemory::EVENT_STORAGE_BYTES - 1,
                                         Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES, false,
                                         Furble::MQTTMemory::EVENT_STORAGE_BYTES,
                                         Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES),
          "start gate ignored the event copies in the free floor");
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
  // A retryable loss retires the client instead of leaving esp-mqtt to its own
  // fixed period, so the replacement arrives off the backoff deadline.
  require(mqtt.hostRetryDelayMs() == 10000, "broker link loss did not schedule the first retry");
  require(!mqtt.hostBlocked(), "a broker link loss blocked the retry policy");
  host_mqtt_timer::advance(10000);
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.isConnected(), "the backoff retry did not restore the broker link");
  require(mqtt.hostRetryDelayMs() == 0 && mqtt.hostConnectFailures() == 0,
          "a live session did not reset the retry policy");
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

  // `cmd/ota` end to end over mock MQTT events. The transport is the
  // production HTTPS adapter over a scripted client, so the topic handler,
  // preconditions, engine, status record and retained policy all run without a
  // network or flash access.
  OtaClient ota_client;
  Furble::OTA::HTTPS::Adapter ota_adapter(ota_client);
  Furble::OTA::Engine ota_engine(ota_adapter);
  OtaSource ota_source;
  OtaDetail ota_detail;
  Furble::OTA::Command::Handler ota_handler(ota_engine, ota_source, mqtt.otaContext(), &ota_detail);
  mqtt.attachOTAHandler(&ota_handler);

  const std::string ota_url = "https://updates.example/furble.bin";
  const std::string ota_topic = root_topic + "/cmd/ota";
  const std::string ota_status_topic = root_topic + "/state/ota";

  host_mqtt::deliver(ota_topic, "check " + ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"available\":\"1.3.0\"")
              != std::string::npos,
          "cmd/ota check did not publish the available version");
  require(
      latestPublishedTopic(ota_status_topic).payload.find("\"update\":true") != std::string::npos,
      "cmd/ota check did not publish update availability");
  require(ota_client.openCalls == 0, "cmd/ota check started a download");
  require(!latestPublishedTopic(ota_status_topic).retain, "a check result was retained");

  // An active camera session owns the radio, so the URL is refused.
  Control::setState(Control::STATE_ACTIVE);
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"camera_session\"")
              != std::string::npos,
          "cmd/ota did not refuse an active camera session");
  require(latestPublishedTopic(root_topic + "/state/error").payload
              == "ota update refused: camera_session",
          "cmd/ota camera refusal was not published on state/error");
  require(ota_client.openCalls == 0, "a refused cmd/ota started a download");

  // A gauge below the floor refuses too, and real external power exempts it.
  Furble::Platform::battery_caps_t caps;
  caps.level = true;
  Furble::Platform::setBatteryCaps(caps);
  Furble::Platform::battery_t battery;
  battery.level = 10;
  Furble::Platform::setBattery(battery);
  Control::setState(Control::STATE_IDLE);
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"battery_low\"")
              != std::string::npos,
          "cmd/ota did not refuse a low battery");
  require(ota_client.openCalls == 0, "a low-battery cmd/ota started a download");

  // The charge *capability* must not exempt the floor. It is a compile-time
  // constant, true on every board that can report a charge state, and reading
  // it as the state let a nearly flat StickS3 start a flash write. Only a VBUS
  // measurement is evidence of external power.
  caps.charging = true;
  Furble::Platform::setBatteryCaps(caps);
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"battery_low\"")
              != std::string::npos,
          "the charge capability exempted the battery floor");
  require(ota_client.openCalls == 0, "the charge capability started a download on a flat cell");

  // A zero VBUS reading is battery power even when the charge flag says
  // otherwise, because a failed PMIC read reports "charging".
  Furble::Platform::battery_status_t status;
  status.charging = true;
  status.vbus = 0;
  Furble::Platform::setBatteryStatus(status);
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"battery_low\"")
              != std::string::npos,
          "a failed PMIC read exempted the battery floor");

  // A positive VBUS reading is external power, so the floor does not apply and
  // the next gate answers instead: the same installed version is not newer.
  status.vbus = 5000;
  Furble::Platform::setBatteryStatus(status);
  const std::string source_version = ota_source.version;
  ota_source.version = FURBLE_VERSION;
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"no_newer_version\"")
              != std::string::npos,
          "a positive VBUS reading did not exempt the battery floor");
  require(ota_client.openCalls == 0, "the external-power probe started a download");
  ota_source.version = source_version;
  caps.charging = false;
  Furble::Platform::setBatteryCaps(caps);
  status.vbus = 0;
  Furble::Platform::setBatteryStatus(status);

  battery.level = 80;
  Furble::Platform::setBattery(battery);
  ota_client.plan = {
      {false, false, 400 },
      {false, true,  1000}
  };
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(ota_engine.busy(), "an admitted cmd/ota did not start the engine");
  require(latestPublishedTopic(ota_status_topic).payload.find("\"state\":\"downloading\"")
              != std::string::npos,
          "an admitted cmd/ota did not report the download");

  // A duplicate QoS-1 delivery must not start a second download.
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(ota_client.openCalls == 1, "a duplicate cmd/ota opened the image twice");
  require(latestPublishedTopic(ota_status_topic).payload.find("\"result\":\"duplicate\"")
              != std::string::npos,
          "a duplicate cmd/ota was not reported as a duplicate");

  // Advance the engine through the mock download to a retained final result.
  for (size_t step = 0; (step < 16) && ota_engine.busy(); step++) {
    ownerStep(mqtt);
  }
  require(!ota_engine.busy(), "the mock cmd/ota download did not finish");
  require(ota_engine.snapshot().state == Furble::OTA::State::Done,
          "the mock cmd/ota download did not report done");
  const auto done = latestPublishedTopic(ota_status_topic);
  require(done.retain, "the final cmd/ota result was not retained");
  require(done.payload.find("\"state\":\"done\"") != std::string::npos,
          "the final cmd/ota result did not report done");
  require(ota_handler.status().progress == 100, "the final progress was not 100 percent");

  // Retained OTA commands are refused before the handler.
  const size_t ota_puts_before_retained = ota_client.openCalls;
  host_mqtt::deliverRetained(ota_topic, ota_url);
  ownerStep(mqtt);
  require(ota_client.openCalls == ota_puts_before_retained,
          "a retained cmd/ota started a download");
  require(
      latestPublishedTopic(root_topic + "/state/error").payload == "retained MQTT command rejected",
      "a retained cmd/ota did not publish a rejection");

  // A failed health check is reported as a rollback on the status topic.
  Furble::OTA::BootReport rolledBack;
  rolledBack.slot = Furble::OTA::RollbackState::Aborted;
  rolledBack.action = Furble::OTA::BootAction::MarkInvalidRollbackAndReboot;
  rolledBack.failure = Furble::OTA::BootFailure::Control;
  mqtt.setOTABootReport(rolledBack);
  host_mqtt::deliver(ota_topic, "status");
  ownerStep(mqtt);
  require(latestPublishedTopic(ota_status_topic).payload.find("\"slot\":\"aborted\"")
              != std::string::npos,
          "state/ota did not report the rollback");
  require(latestPublishedTopic(ota_status_topic)
                  .payload.find("\"boot\":\"mark_invalid_rollback_and_reboot\"")
              != std::string::npos,
          "state/ota did not report the rollback action");

  // Without a linked transport the topic still answers with the documented
  // refusal, so a dashboard can tell a build without OTA from an idle one.
  mqtt.attachOTAHandler(nullptr);
  host_mqtt::deliver(ota_topic, ota_url);
  ownerStep(mqtt);
  require(
      latestPublishedTopic(ota_status_topic).payload.find("\"reason\":\"transport_unavailable\"")
          != std::string::npos,
      "cmd/ota without a transport did not report transport_unavailable");
  require(latestPublishedTopic(root_topic + "/state/error").payload
              == "ota transport not linked in this build",
          "cmd/ota without a transport did not publish the reason");

  // Restore the shared doubles for the shutter and interval checks below.
  Furble::Platform::setBatteryCaps(Furble::Platform::battery_caps_t {});
  Control::setState(Control::STATE_ACTIVE);

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
  require(host_mqtt::startCount() == 4,
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
  require(host_mqtt::startCount() == 4, "reload did not create a replacement MQTT client");
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
  require(host_mqtt::startCount() == 5,
          "reload timeout did not destroy the old client and create a replacement");
  ownerStep(mqtt);
  require(publishedTopic(timeout_root_topic + "/status").payload == "online",
          "timeout replacement did not publish on its reloaded topic");

  // The old client task can deliver one more callback while destroy joins it,
  // and the replacement can reuse its address. The queue is cleared after
  // destroy, so that command never runs on the replacement session. The
  // reload keeps the same topics, so the stale command would match them.
  host_mqtt::emitDataOnDestroy(timeout_root_topic + "/cmd/shutter", "press");
  mqtt.reloadSetting();
  ownerStep(mqtt);
  host_mqtt::emitPublished(latestPublishedTopic(timeout_root_topic + "/status").message_id);
  ownerStep(mqtt);
  require(host_mqtt::startCount() == 6, "reload did not create the recycled-address client");
  const size_t beforeRecycled = Control::commands().size();
  ownerStep(mqtt);
  require(mqtt.isConnected(), "the recycled-address client did not connect");
  require(latestPublishedTopic(timeout_root_topic + "/status").payload == "online",
          "the recycled-address client did not publish online");
  require(std::none_of(Control::commands().begin() + beforeRecycled, Control::commands().end(),
                       [](const auto command) { return command == Control::CMD_SHUTTER_PRESS; }),
          "a command queued during destroy ran on the next client");

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
  require(mqtt.hostRetryDelayMs() == 10000, "a refused TLS start did not schedule the retry");
  host_mqtt_heap::reset();
  host_mqtt_timer::advance(10000);
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

  // The retry policy: a broker outage never blocks, the gap doubles to the cap
  // and stays there, and only a classified credentials or configuration
  // rejection stops the client.
  mqtt.disconnect();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.hostRetryDelayMs() == 0, "a manual disconnect scheduled a retry");
  Settings::mqttURI = "mqtt://loopback";
  mqtt.reloadSetting();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.isConnected(), "the retry policy fixture did not connect");

  host_mqtt::dropConnection();
  ownerStep(mqtt);
  require(!mqtt.isConnected(), "the retry policy fixture stayed connected");
  require(mqtt.hostConnectFailures() == 1, "a retryable loss was not counted");
  require(mqtt.hostRetryDelayMs() == 10000, "the first retry gap is not 10 s");
  require(!mqtt.hostBlocked(), "a retryable loss blocked the retry policy");

  // One failed attempt per deadline, doubling to the 60 s cap.
  const uint32_t expected_gaps[] = {20000, 40000, 60000, 60000};
  uint32_t previous_gap = mqtt.hostRetryDelayMs();
  for (const uint32_t expected : expected_gaps) {
    const int attempts_before = host_mqtt::startAttempts();
    host_mqtt_timer::advance(previous_gap - 1);
    ownerStep(mqtt);
    require(host_mqtt::startAttempts() == attempts_before,
            "a retry attempt ran before its backoff deadline");
    host_mqtt::failNextStart();
    host_mqtt_timer::advance(1);
    ownerStep(mqtt);
    require(host_mqtt::startAttempts() == attempts_before + 1,
            "the backoff deadline did not admit the next attempt");
    require(mqtt.hostRetryDelayMs() == expected, "the retry gap did not reach its next step");
    require(!mqtt.hostBlocked(), "an outage blocked the retry policy");
    previous_gap = expected;
  }
  require(mqtt.hostConnectFailures() == 5, "the outage did not count every failed attempt");

  // A classified credentials rejection stops the client until `mqtt connect`,
  // which reloads the settings and clears the block.
  Settings::mqttURI.clear();
  mqtt.reloadSetting();
  Settings::mqttURI = "mqtt://loopback";
  host_mqtt::refuseNextConnect(MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED);
  mqtt.reloadSetting();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.hostBlocked(), "a refused CONNACK did not stop the retry policy");
  require(mqtt.hostRetryDelayMs() == 0, "a blocked retry kept a pending gap");
  require(!mqtt.isConnected(), "a refused CONNACK reported a connection");
  const int blocked_attempts = host_mqtt::startAttempts();
  ownerStep(mqtt);
  host_mqtt_timer::advance(600000);
  ownerStep(mqtt);
  require(host_mqtt::startAttempts() == blocked_attempts, "a blocked retry kept starting clients");
  mqtt.reloadSetting();
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(!mqtt.hostBlocked() && mqtt.isConnected(),
          "mqtt connect did not clear the classified block");

  // A protocol or client-id rejection is the other permanent class. A broker
  // that reports itself unavailable stays retryable.
  for (const int refused : {MQTT_CONNECTION_REFUSE_PROTOCOL, MQTT_CONNECTION_REFUSE_ID_REJECTED}) {
    host_mqtt::refuseNextConnect(refused);
    host_mqtt::emitDisconnected();
    ownerStep(mqtt);
    host_mqtt_timer::advance(10000);
    ownerStep(mqtt);
    ownerStep(mqtt);
    require(mqtt.hostBlocked(), "a refused protocol or client id did not block");
    mqtt.reloadSetting();
    ownerStep(mqtt);
    ownerStep(mqtt);
  }
  require(mqtt.isConnected(), "the retry policy fixture did not reconnect");

  host_mqtt::refuseNextConnect(MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE);
  host_mqtt::emitDisconnected();
  ownerStep(mqtt);
  host_mqtt_timer::advance(10000);
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(!mqtt.hostBlocked(), "an unavailable broker blocked instead of retrying");
  require(mqtt.hostRetryDelayMs() == 20000,
          "an unavailable broker did not schedule the next retry");
  host_mqtt_timer::advance(20000);
  ownerStep(mqtt);
  ownerStep(mqtt);
  require(mqtt.isConnected(), "the retry after an unavailable broker did not connect");

  // A broker outage must not freeze a running transfer. The engine has no task
  // of its own, so the owner has to keep stepping it while the broker is down;
  // otherwise the update sits busy() with no command able to reach it and the
  // staged image held until the broker comes back. Lifecycle events are drained
  // before the owner's publish block, so a drop seen on this iteration is the
  // state the step decision is made in. Placed last because the extra client
  // replacement moves every earlier startCount() expectation.
  {
    OtaClient outage_client;
    Furble::OTA::HTTPS::Adapter outage_adapter(outage_client);
    Furble::OTA::Engine outage_engine(outage_adapter);
    OtaSource outage_source;
    Furble::OTA::Command::Handler outage_handler(outage_engine, outage_source, mqtt.otaContext());
    mqtt.attachOTAHandler(&outage_handler);

    Control::setState(Control::STATE_IDLE);
    Furble::Platform::battery_caps_t outage_caps;
    outage_caps.level = true;
    Furble::Platform::setBatteryCaps(outage_caps);
    Furble::Platform::battery_t outage_battery;
    outage_battery.level = 90;
    Furble::Platform::setBattery(outage_battery);
    Furble::Platform::battery_status_t outage_status;
    outage_status.charging = false;
    outage_status.vbus = 0;
    Furble::Platform::setBatteryStatus(outage_status);

    outage_client.plan = {
        {false, false, 400},
        {false, true,  800}
    };
    const std::string outage_url = "https://updates.example/furble-outage.bin";
    host_mqtt::deliver(timeout_root_topic + "/cmd/ota", outage_url);
    ownerStep(mqtt);
    require(outage_engine.busy(), "the outage probe did not start an update");

    const size_t outage_polls = outage_client.pollCalls;
    host_mqtt::dropConnection();
    ownerStep(mqtt);
    require(!mqtt.isConnected(), "the mock broker drop did not reach the owner");
    require(outage_client.pollCalls > outage_polls,
            "the OTA engine stalled while the broker was down");
    mqtt.attachOTAHandler(nullptr);
  }

  mqtt.disconnect();
  ownerStep(mqtt);
  host_mqtt_timer::advance(1000);
  ownerStep(mqtt);

  std::cout << "mqtt host loopback: PASS\n";
  return EXIT_SUCCESS;
}
