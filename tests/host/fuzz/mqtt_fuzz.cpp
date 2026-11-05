// Deterministic API fuzzer for the production MQTT owner task.
//
// The real FurbleMQTT.cpp runs against the host mosquitto client double one
// owner step at a time. The fuzzer delivers malformed topics, oversized and
// fragmented payloads, retained wildcard traffic, invalid JSON and repeated
// bursts, then asserts that only an exact subscribed command can reach Control
// or request the UI. The same seed always produces the same delivery stream.

#include <algorithm>
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
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "cJSON.h"
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

  auto &mqtt = MQTT::getInstance();
  MQTT::init();
  host_mqtt_network::setGotIp("ethernet", 0x0a00002a);
  mqtt.hostTaskStep();
  mqtt.hostTaskStep();
  require(mqtt.isConnected(), "MQTT owner did not connect to the host broker");

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
