// Deterministic API fuzzer for the production MQTT owner task.
//
// The real FurbleMQTT.cpp runs against the host mosquitto client double one
// owner step at a time. The fuzzer delivers malformed topics, oversized and
// fragmented payloads, retained wildcard traffic, invalid JSON and repeated
// bursts, then asserts that only an exact subscribed command can reach Control
// or request the UI. The same seed always produces the same delivery stream.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "CameraList.h"
#include "Device.h"
#include "FurbleMQTT.h"
#include "FurbleMQTTMemory.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "mqtt_host_dependencies.h"

namespace {

int failures = 0;
std::string g_Context;

void fail(const std::string &message) {
  std::cerr << "MQTT FUZZ FINDING [" << g_Context << "] " << message << '\n';
  failures++;
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

std::string randomBytes(std::mt19937_64 &rng, size_t length) {
  std::string value(length, '\0');
  for (auto &character : value) {
    character = static_cast<char>(rng() & 0xff);
  }
  return value;
}

bool isCommandTopic(const std::string &topic, const std::string &root) {
  if (topic == root + "/cmd/shutter" || topic == root + "/cmd/focus"
      || topic == root + "/cmd/interval" || topic == root + "/cmd/connect"
      || topic == root + "/cmd/disconnect" || topic == root + "/cmd/location") {
    return true;
  }
  return false;
}

size_t discoveryPublications(void) {
  const auto &messages = host_mqtt::published();
  return static_cast<size_t>(std::count_if(messages.begin(), messages.end(), [](const auto &message) {
    return message.topic.rfind("homeassistant/device/", 0) == 0 && !message.payload.empty();
  }));
}

const host_mqtt::PublishedMessage *latestDiscovery(void) {
  const auto &messages = host_mqtt::published();
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    if (it->topic.rfind("homeassistant/device/", 0) == 0 && !it->payload.empty()) {
      return &*it;
    }
  }
  return nullptr;
}

void runGatePermutations(void) {
  using namespace Furble::MQTTMemory;
  static_assert(START_INTERNAL_FREE_FLOOR == 8192, "start free floor changed");
  static_assert(START_INTERNAL_LARGEST_FLOOR == 4096, "start largest floor changed");
  static_assert(DISCOVERY_INTERNAL_FREE_FLOOR == 10240, "discovery free floor changed");
  static_assert(DISCOVERY_INTERNAL_LARGEST_FLOOR == 4096, "discovery largest floor changed");
  const std::array<size_t, 7> values = {
      0, 1, 4095, 4096, 8191, 8192, 10240};
  for (size_t freeBytes : values) {
    for (size_t largest : values) {
      const auto start = startGate(freeBytes, largest);
      require(start.open
                  == (freeBytes >= START_INTERNAL_FREE_FLOOR
                      && largest >= START_INTERNAL_LARGEST_FLOOR),
              "start gate disagreed at a boundary");
      const auto discovery = discoveryGate(freeBytes, largest);
      require(discovery.open
                  == (freeBytes >= DISCOVERY_INTERNAL_FREE_FLOOR
                      && largest >= DISCOVERY_INTERNAL_LARGEST_FLOOR),
              "discovery gate disagreed at a boundary");
    }
  }
  std::mt19937_64 rng(0x47544145ULL);
  for (uint32_t iteration = 0; iteration < 5000; iteration++) {
    const size_t freeBytes = static_cast<size_t>(rng() % 32768);
    const size_t largest = static_cast<size_t>(rng() % (freeBytes + 1));
    const auto start = startGate(freeBytes, largest);
    require(start.open
                == (freeBytes >= START_INTERNAL_FREE_FLOOR
                    && largest >= START_INTERNAL_LARGEST_FLOOR),
            "random start gate disagreed");
    const auto discovery = discoveryGate(freeBytes, largest);
    require(discovery.open
                == (freeBytes >= DISCOVERY_INTERNAL_FREE_FLOOR
                    && largest >= DISCOVERY_INTERNAL_LARGEST_FLOOR),
            "random discovery gate disagreed");
  }
}

void runDiscoveryBoundaries(Furble::MQTT &mqtt) {
  using namespace Furble::MQTTMemory;
  static const std::string status = "homeassistant/status";
  host_mqtt_heap::set(DISCOVERY_INTERNAL_FREE_FLOOR - 1, DISCOVERY_INTERNAL_LARGEST_FLOOR);
  size_t before = discoveryPublications();
  host_mqtt::brokerPublish(status, "online", 0, true);
  mqtt.hostTaskStep();
  require(discoveryPublications() == before, "discovery published below the free floor");

  host_mqtt_heap::set(DISCOVERY_INTERNAL_FREE_FLOOR, DISCOVERY_INTERNAL_LARGEST_FLOOR - 1);
  before = discoveryPublications();
  host_mqtt::brokerPublish(status, "online", 0, true);
  mqtt.hostTaskStep();
  require(discoveryPublications() == before, "discovery published below the largest floor");

  host_mqtt_heap::set(DISCOVERY_INTERNAL_FREE_FLOOR, DISCOVERY_INTERNAL_LARGEST_FLOOR);
  before = discoveryPublications();
  host_mqtt::brokerPublish(status, "online", 0, true);
  mqtt.hostTaskStep();
  require(discoveryPublications() == before + 2, "discovery did not publish at the exact floor");

  // A fragmented retained HA status must not be treated as a complete replay.
  before = discoveryPublications();
  host_mqtt::deliverFragmented(status, "online", 0, 10);
  mqtt.hostTaskStep();
  require(discoveryPublications() == before, "fragmented HA status triggered discovery");

  // Discovery payloads deliberately exceed the 1,536-byte network buffer. The
  // broker fragments them on the wire; the retained record must still be the
  // complete valid JSON document and survive repeated HA online replays.
  const auto *hub = latestDiscovery();
  require(hub != nullptr, "discovery published no hub record");
  if (hub != nullptr) {
    require(hub->payload.size() > BUFFER_SIZE,
            "discovery payload did not exercise the 1,536-byte fragmentation path");
    cJSON *parsed = cJSON_ParseWithLength(hub->payload.data(), hub->payload.size());
    require(parsed != nullptr, "discovery payload is not complete JSON after publication");
    cJSON_Delete(parsed);
  }
  for (int replay = 0; replay < 5; replay++) {
    before = discoveryPublications();
    host_mqtt::brokerPublish(status, "online", 0, true);
    mqtt.hostTaskStep();
    require(discoveryPublications() == before + 2,
            "repeated HA online replay did not republish both records");
  }
  require(host_mqtt::hasRetained("homeassistant/device/furble_hub-fuzz/config"),
          "hub discovery record was not retained after replays");
  host_mqtt_heap::reset();
}

}  // namespace

int main(int argc, char **argv) {
  uint64_t seed = 1;
  uint32_t iterations = 500;
  bool discoveryOOM = false;
  for (int index = 1; index < argc; index++) {
    const std::string argument = argv[index];
    if (argument == "--seed" && index + 1 < argc) {
      seed = std::strtoull(argv[++index], nullptr, 10);
    } else if (argument == "--iterations" && index + 1 < argc) {
      iterations = static_cast<uint32_t>(std::strtoul(argv[++index], nullptr, 10));
    } else if (argument == "--discovery-oom") {
      discoveryOOM = true;
    } else {
      std::cerr << "usage: mqtt_fuzz [--seed N] [--iterations N] [--discovery-oom]\n";
      return 2;
    }
  }
  if (iterations == 0) {
    std::cerr << "iterations must be non-zero\n";
    return 2;
  }

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
  Device::setStringID("hub-fuzz");
  host_mqtt::reset();
  host_mqtt_network::reset();
  host_mqtt_timer::reset();

  auto camera = std::make_shared<Camera>("camera-fuzz", "X100VI", Camera::Type::FUJIFILM_BASIC);
  CameraList::setCameras({camera});
  UI::reset();

  runGatePermutations();

  auto &mqtt = MQTT::getInstance();
  MQTT::init();
  host_mqtt_heap::set(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR - 1,
                      Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR);
  host_mqtt_network::setGotIp("ethernet", 0x0a00002a);
  mqtt.hostTaskStep();
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 0,
          "MQTT client started below the internal-heap start floor");

  host_mqtt_heap::set(Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR,
                      Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR);
  mqtt.reloadSetting();
  mqtt.hostTaskStep();
  mqtt.hostTaskStep();
  require(host_mqtt::startCount() == 1,
          "MQTT client did not start at the exact internal-heap floor");
  require(host_mqtt::lastTaskStackSize() == Furble::MQTTMemory::PLAIN_WORKER_STACK_BYTES,
          "plain MQTT worker did not use the 3 KiB stack");
  require(host_mqtt::lastBufferSize() == Furble::MQTTMemory::BUFFER_SIZE,
          "MQTT network buffer did not use the 1,536-byte budget");
  require(mqtt.isConnected(), "MQTT owner did not connect to the host broker");
  runDiscoveryBoundaries(mqtt);

  const std::string root = "furble/hub-fuzz";
  const std::string cameraId = std::to_string(CameraList::getCameraId(camera.get()));
  std::mt19937_64 rng(seed);

  for (uint32_t iteration = 0; iteration < iterations; iteration++) {
    g_Context = "iteration-" + std::to_string(iteration);
    const uint32_t mode = static_cast<uint32_t>(rng() % 12);
    std::string topic;
    std::string payload;
    bool fragmented = false;
    bool retained = false;
    switch (mode) {
      case 0:
        topic = root + "/cmd/shutter";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 1:
        topic = root + "/cmd/focus";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 2:
        topic = root + "/cmd/interval";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 3:
        topic = root + "/cmd/connect";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 8));
        break;
      case 4:
        topic = root + "/cmd/disconnect";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 8));
        break;
      case 5:
        topic = root + "/cmd/location";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 512));
        break;
      case 6:
        topic = root + "/cmd/unknown";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 7:
        topic = "homeassistant/status";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 16));
        break;
      case 8:
        topic = root + "/cmd/" + randomBytes(rng, static_cast<size_t>(rng() % 128));
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 9:
        topic = std::string(static_cast<size_t>(rng() % 300), '/');
        payload = randomBytes(rng, static_cast<size_t>(rng() % 64));
        break;
      case 10:
        topic = root + "/cmd/shutter";
        payload = randomBytes(rng, 1024 + static_cast<size_t>(rng() % 4096));
        fragmented = true;
        break;
      default:
        topic = root + "/cmd/interval";
        payload = randomBytes(rng, static_cast<size_t>(rng() % 32));
        retained = true;
        break;
    }

    const size_t commandsBefore = Control::commands().size();
    const size_t requestsBefore = UI::requests().size();
    if (retained) {
      host_mqtt::deliverRetained(topic, payload);
    } else if (fragmented) {
      host_mqtt::deliverFragmented(topic, payload, 0, 8);
    } else {
      host_mqtt::deliver(topic, payload);
    }
    if ((iteration % 7) == 0) {
      host_mqtt::emitDataBurst(topic, payload, 4);
    }
    mqtt.hostTaskStep();

    if (!isCommandTopic(topic, root) && topic != "homeassistant/status") {
      require(Control::commands().size() == commandsBefore,
              "an unsubscribed topic reached Control");
      require(UI::requests().size() == requestsBefore,
              "an unsubscribed topic reached the UI request queue");
    }
    require(mqtt.isConnected(), "the owner lost its broker link during a malformed delivery");

    // The owner may publish a rejection, but the callback copies must stay
    // bounded. No payload is allowed to grow an unbounded response queue.
    require(host_mqtt::published().size() < 100000, "the publication log grew without bound");
  }

  // Home Assistant discovery under an allocation-failure schedule. The real
  // discovery builder allocates a cJSON tree and a printed payload per camera.
  // At 1.7 KiB free on the StickS3 the hardware saw that build fail; every
  // failure path must free what it allocated and leave the link alive.
  if (discoveryOOM) {
    g_Context = "discovery-oom";
    host_cjson::reset();
    for (uint32_t iteration = 0; iteration < 256; iteration++) {
      const uint32_t percent = 1 + static_cast<uint32_t>(rng() % 100);
      host_cjson::setFailureSchedule(seed + iteration, percent);
      host_mqtt::brokerPublish("homeassistant/status", "online", 0, true);
      mqtt.hostTaskStep();
      if (host_cjson::liveAllocations() != 0) {
        fail("discovery path leaked " + std::to_string(host_cjson::liveAllocations())
             + " cJSON allocation(s) at failure percent " + std::to_string(percent));
        host_cjson::reset();
      }
      if (!mqtt.isConnected()) {
        fail("discovery allocation failure dropped the broker link");
      }
    }
    host_cjson::reset();
  }

  // A valid command still works after the malformed stream.
  g_Context = "post-fuzz";
  host_cjson::reset();
  const size_t commandsBefore = Control::commands().size();
  host_mqtt::deliver(root + "/cmd/shutter", "press");
  mqtt.hostTaskStep();
  require(Control::commands().size() == commandsBefore + 1,
          "valid shutter press failed after fuzzing");
  require(Control::commands().back() == Control::CMD_SHUTTER_PRESS,
          "valid shutter press routed to the wrong command");

  std::cout << "MQTT FUZZ SUMMARY seed=" << seed << " iterations=" << iterations
            << " discovery_oom=" << (discoveryOOM ? "ran" : "skipped") << " findings=" << failures
            << '\n';
  return failures == 0 ? 0 : 1;
}
