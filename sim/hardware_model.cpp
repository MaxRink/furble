#include "hardware_model.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleMQTTMemory.h"
#include "clock.h"
#include "crash_log_sim.h"
#include "driver.h"
#include "heap_model.h"

namespace {

constexpr const char *LOG_TAG = "hardware";

struct board_caps_t {
  const char *mcu;
  uint32_t flashKb;
  const char *pmic;
  const char *gauge;
  const char *psram;
  const char *usb;
  const char *display;
  int width;
  int height;
  bool touch;
  bool battery;
  bool wifi;
  bool ethernet;
  bool ble;
  bool ir;
  bool sd;
  bool audio;
  bool rtc;
};

const board_caps_t STICKC = {.mcu = "esp32",
                             .flashKb = 4096,
                             .pmic = "axp192",
                             .gauge = "voltage_current",
                             .psram = "absent",
                             .usb = "uart",
                             .display = "80x160",
                             .width = 80,
                             .height = 160,
                             .touch = false,
                             .battery = true,
                             .wifi = true,
                             .ethernet = false,
                             .ble = true,
                             .ir = true,
                             .sd = false,
                             .audio = true,
                             .rtc = true};
const board_caps_t STICKC_PLUS = {.mcu = "esp32",
                                  .flashKb = 4096,
                                  .pmic = "axp192",
                                  .gauge = "voltage_current",
                                  .psram = "absent",
                                  .usb = "uart",
                                  .display = "135x240",
                                  .width = 135,
                                  .height = 240,
                                  .touch = false,
                                  .battery = true,
                                  .wifi = true,
                                  .ethernet = false,
                                  .ble = true,
                                  .ir = true,
                                  .sd = false,
                                  .audio = true,
                                  .rtc = true};
const board_caps_t STICKC_PLUS2 = {.mcu = "esp32",
                                   .flashKb = 4096,
                                   .pmic = "none",
                                   .gauge = "voltage",
                                   .psram = "absent",
                                   .usb = "uart",
                                   .display = "135x240",
                                   .width = 135,
                                   .height = 240,
                                   .touch = false,
                                   .battery = true,
                                   .wifi = true,
                                   .ethernet = false,
                                   .ble = true,
                                   .ir = true,
                                   .sd = false,
                                   .audio = true,
                                   .rtc = true};
const board_caps_t STICKS3 = {.mcu = "esp32s3",
                              .flashKb = 8192,
                              .pmic = "m5pm1",
                              .gauge = "voltage_charge",
                              .psram = "octal",
                              .usb = "serial_jtag",
                              .display = "135x240",
                              .width = 135,
                              .height = 240,
                              .touch = false,
                              .battery = true,
                              .wifi = true,
                              .ethernet = false,
                              .ble = true,
                              .ir = true,
                              .sd = false,
                              .audio = true,
                              .rtc = true};
const board_caps_t CORE = {.mcu = "esp32",
                           .flashKb = 4096,
                           .pmic = "ip5306",
                           .gauge = "four_level",
                           .psram = "absent",
                           .usb = "uart",
                           .display = "320x240",
                           .width = 320,
                           .height = 240,
                           .touch = false,
                           .battery = true,
                           .wifi = true,
                           .ethernet = false,
                           .ble = true,
                           .ir = true,
                           .sd = true,
                           .audio = true,
                           .rtc = true};
const board_caps_t CORE2 = {.mcu = "esp32",
                            .flashKb = 16384,
                            .pmic = "axp192",
                            .gauge = "voltage_current",
                            .psram = "absent",
                            .usb = "uart",
                            .display = "320x240",
                            .width = 320,
                            .height = 240,
                            .touch = true,
                            .battery = true,
                            .wifi = true,
                            .ethernet = false,
                            .ble = true,
                            .ir = true,
                            .sd = true,
                            .audio = true,
                            .rtc = true};
const board_caps_t TOUGH = {.mcu = "esp32",
                            .flashKb = 16384,
                            .pmic = "axp192",
                            .gauge = "voltage_current",
                            .psram = "absent",
                            .usb = "uart",
                            .display = "320x240",
                            .width = 320,
                            .height = 240,
                            .touch = true,
                            .battery = true,
                            .wifi = true,
                            .ethernet = false,
                            .ble = true,
                            .ir = true,
                            .sd = true,
                            .audio = true,
                            .rtc = true};
const board_caps_t HEADLESS = {.mcu = "esp32s3",
                               .flashKb = 8192,
                               .pmic = "none",
                               .gauge = "none",
                               .psram = "octal",
                               .usb = "serial_jtag",
                               .display = "none",
                               .width = 0,
                               .height = 0,
                               .touch = false,
                               .battery = false,
                               .wifi = true,
                               .ethernet = false,
                               .ble = true,
                               .ir = false,
                               .sd = false,
                               .audio = false,
                               .rtc = true};
const board_caps_t WAVESHARE = {.mcu = "esp32s3",
                                .flashKb = 16384,
                                .pmic = "none",
                                .gauge = "none",
                                .psram = "octal",
                                .usb = "serial_jtag",
                                .display = "none",
                                .width = 0,
                                .height = 0,
                                .touch = false,
                                .battery = false,
                                .wifi = false,
                                .ethernet = true,
                                .ble = true,
                                .ir = false,
                                .sd = true,
                                .audio = false,
                                .rtc = true};

struct pmic_state_t {
  std::string variant;
  std::string gauge;
  bool batteryPresent = true;
  bool charging = false;
  bool chargeEnabled = true;
  int batteryPercent = 80;
  int batteryMillivolts = 4000;
  int chargeCurrentMa = 0;
  bool watchdogEnabled = false;
  uint32_t watchdogFeeds = 0;
  bool downloadLocked = true;
  bool powerLatch = true;
  uint32_t brownouts = 0;
};

enum class power_state_t { ON, OFF, DOWNLOAD };
enum class usb_mode_t { APP, ROM, ESPTOOL };

struct power_button_t {
  power_state_t state = power_state_t::ON;
  uint32_t clicks = 0;
  uint32_t doubleClicks = 0;
  uint32_t longPresses = 0;
  uint32_t resets = 0;
  uint32_t offs = 0;
  uint32_t downloads = 0;
  uint32_t recoveries = 0;
};

struct usb_state_t {
  usb_mode_t mode = usb_mode_t::APP;
  bool hostOpen = false;
  bool dtr = false;
  bool rts = false;
  uint32_t romEntries = 0;
  uint32_t syncs = 0;
  uint32_t recoveries = 0;
};

struct wifi_state_t {
  std::string state = "disabled";
  std::string reason = "none";
  std::string ip = "192.0.2.10";
  std::string ssid = "sim-ap";
  int rssi = -55;
  uint32_t attempts = 0;
  uint32_t retryMs = 0;
  bool ntpEnabled = true;
  bool ntpSynced = false;
  int64_t ntpOffsetUs = 0;
};

struct net_state_t {
  std::string ethernet = "down";
  uint32_t linkUps = 0;
  uint32_t linkDowns = 0;
};

struct ble_state_t {
  std::string bond = "none";
  std::string auth = "idle";
  std::string gattCache = "valid";
  std::string companion = "off";
  uint32_t authTimeouts = 0;
  uint32_t reidentifyCount = 0;
  uint32_t reconnects = 0;
  uint32_t rejectedLinks = 0;
};

struct peripheral_state_t {
  std::string audio = "idle";
  std::string ir = "idle";
  std::string sd = "absent";
  uint32_t audioOvercurrents = 0;
  uint32_t irTimeouts = 0;
  uint32_t sdErrors = 0;
};

struct mqtt_model_state_t {
  bool enabled = false;
  size_t ownerStack = 10240;
  size_t workerStack = 4096;
  bool tls = false;
  bool expectOverflow = false;
  bool ha = false;
  size_t bufferBytes = 2048;
  std::string haState = "off";
  size_t haPeak = 0;
  std::string ownerGate = "open";
  std::string startGate = "open";
  std::string discoveryGate = "open";
  std::string haCache = "valid";
  uint32_t discoveryFragments = 0;
  size_t queueBytes = 0;
  std::string queueRegion = "none";
  size_t cameraBytes = 0;
  size_t mainStackBytes = 12288;
  size_t mainHighWater = 3476;
  std::string state = "off";
  bool overflow = false;
  size_t heapFreeAfter = 0;
};

struct hardware_state_t {
  std::mutex mutex;
  std::string profile;
  board_caps_t caps = STICKS3;
  pmic_state_t pmic;
  power_button_t power;
  usb_state_t usb;
  wifi_state_t wifi;
  net_state_t net;
  ble_state_t ble;
  peripheral_state_t peripheral;
  mqtt_model_state_t mqtt;
  uint64_t leakProbe = 0;
  std::string bootDefaultsMarker = "current";
  bool bootSeedFail = false;
  uint32_t bootDefaultScans = 0;
  uint32_t bootSeedAttempts = 0;
  bool bootMarkerWritten = true;
  bool psramMemtest = true;
  std::string bootProfile = "debug";
  uint32_t resetToAppMs = 1300;
  std::string wifiFastConnect = "usable";
  std::string wifiAuth = "known";
  uint32_t wifiDirectFailures = 0;
  uint32_t wifiScans = 0;
  bool wifiMovedAp = false;
  std::string mqttStartGate = "open";
  std::string mqttDiscoveryGate = "open";
  std::string mqttHaCache = "valid";
  uint32_t mqttDiscoveryFragments = 0;
  size_t mqttDiscoveryPayload = 3005;
  bool companionEnabled = false;
  std::string otaSlot = "ota_0";
  bool otaRollback = false;
  uint32_t otaUpdates = 0;
  uint32_t otaCommits = 0;
  std::string iconState = "idle";
  uint32_t resetReason = ESP_RST_POWERON;
  std::string resetReasonName = "poweron";
  bool psramExplicit = false;
  bool started = false;
  bool failClosed = false;
};

hardware_state_t &state(void) {
  static hardware_state_t instance;
  return instance;
}

const board_caps_t &capsFor(const std::string &profile) {
  if (profile == "stickc") {
    return STICKC;
  }
  if (profile == "stickc-plus") {
    return STICKC_PLUS;
  }
  if (profile == "stickc-plus2") {
    return STICKC_PLUS2;
  }
  if (profile == "core") {
    return CORE;
  }
  if (profile == "core2") {
    return CORE2;
  }
  if (profile == "tough") {
    return TOUGH;
  }
  if (profile == "headless-s3") {
    return HEADLESS;
  }
  if (profile == "waveshare-s3-eth") {
    return WAVESHARE;
  }
  return STICKS3;
}

std::string defaultProfile(void) {
#if defined(FURBLE_M5STICKS3)
  return "sticks3";
#elif defined(FURBLE_M5STICKC_PLUS)
  return "stickc-plus";
#elif defined(FURBLE_M5STICKC)
  return "stickc";
#elif defined(FURBLE_M5COREX)
  return "core";
#else
  return "sticks3";
#endif
}

uint32_t reasonForName(const std::string &name) {
  if (name == "software") {
    return ESP_RST_SW;
  }
  if (name == "panic") {
    return ESP_RST_PANIC;
  }
  if (name == "int_wdt") {
    return ESP_RST_INT_WDT;
  }
  if (name == "task_wdt" || name == "tg1wdt") {
    return ESP_RST_TASK_WDT;
  }
  if (name == "other_wdt") {
    return ESP_RST_WDT;
  }
  if (name == "brownout") {
    return ESP_RST_BROWNOUT;
  }
  if (name == "usb_uart_chip_reset" || name == "usb") {
    return ESP_RST_USB;
  }
  if (name == "jtag") {
    return ESP_RST_JTAG;
  }
  if (name == "power_glitch") {
    return ESP_RST_PWR_GLITCH;
  }
  return ESP_RST_POWERON;
}

const char *reasonName(uint32_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "other_wdt";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_USB:
      return "usb_uart_chip_reset";
    case ESP_RST_JTAG:
      return "jtag";
    case ESP_RST_PWR_GLITCH:
      return "power_glitch";
    default:
      return "poweron";
  }
}

void enterRomLocked(hardware_state_t &hw, const char *reason) {
  hw.usb.mode = usb_mode_t::ROM;
  hw.usb.romEntries++;
  hw.power.state = power_state_t::DOWNLOAD;
  hw.pmic.downloadLocked = false;
  ESP_LOGW(LOG_TAG, "%s; console and BLE are unavailable until a power-button reset", reason);
}

void recoverToAppLocked(hardware_state_t &hw, const char *reason) {
  hw.usb.mode = usb_mode_t::APP;
  hw.usb.recoveries++;
  hw.power.recoveries++;
  hw.power.state = power_state_t::ON;
  hw.resetReason = ESP_RST_POWERON;
  hw.resetReasonName = "poweron";
  ESP_LOGI(LOG_TAG, "%s; application console and BLE are available", reason);
}

void powerOnLocked(hardware_state_t &hw) {
  hw.power.state = power_state_t::ON;
  hw.resetReason = ESP_RST_POWERON;
  hw.resetReasonName = "poweron";
}

void psramProbeTask(void *) {
  vTaskDelay(portMAX_DELAY);
}

}  // namespace

namespace Furble::Sim {

void hwSetProfile(const std::string &profile) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().profile = profile;
  state().caps = capsFor(profile);
}

const char *hwProfile(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().profile.c_str();
}

const char *hwMcu(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.mcu;
}

const char *hwPmicVariant(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.variant.c_str();
}

const char *hwUsbTransport(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.usb;
}

const char *hwGauge(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.gauge.c_str();
}

const char *hwDisplayClass(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().caps.display;
}

void hwSetPsramMode(const std::string &mode) {
  {
    const std::lock_guard<std::mutex> lock(state().mutex);
    state().psramExplicit = true;
  }
  if (mode == "quad" || mode == "octal") {
    const size_t bytes = 8u * 1024u * 1024u;
    heapSetPsram(mode, bytes, bytes / 2);
  } else if (mode == "init_fail") {
    heapSetPsram("init_fail", 0, 0);
  } else {
    heapSetPsram("absent", 0, 0);
  }
}

void hwSetPmicVariant(const std::string &variant) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.variant = variant;
  if (variant == "none") {
    state().pmic.batteryPresent = false;
    state().pmic.chargeEnabled = false;
    state().pmic.powerLatch = false;
    state().pmic.watchdogEnabled = false;
  } else if (variant == "ip5306" || variant == "axp192" || variant == "axp2101") {
    state().pmic.powerLatch = true;
  }
}

void hwSetBatteryGauge(const std::string &gauge) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.gauge = gauge;
}

void hwSetResetReasonName(const std::string &name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().resetReason = reasonForName(name);
  state().resetReasonName = reasonName(state().resetReason);
}

void hwSetBatteryState(const std::string &batteryState) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  pmic_state_t &pmic = state().pmic;
  if (batteryState == "absent") {
    pmic.batteryPresent = false;
    pmic.batteryPercent = 0;
    pmic.batteryMillivolts = 0;
    pmic.charging = false;
  } else {
    pmic.batteryPresent = true;
    if (batteryState == "charging") {
      pmic.charging = true;
      pmic.chargeCurrentMa = 100;
    } else if (batteryState == "full") {
      pmic.charging = false;
      pmic.batteryPercent = 100;
      pmic.batteryMillivolts = 4200;
    } else if (batteryState == "low") {
      pmic.batteryPercent = 4;
      pmic.batteryMillivolts = 3400;
      pmic.charging = false;
    }
  }
}

void hwSetMqttStackModel(bool enabled,
                         size_t ownerStack,
                         size_t workerStack,
                         bool tls,
                         bool expectOverflow) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.enabled = enabled;
  state().mqtt.ownerStack = ownerStack;
  state().mqtt.workerStack = workerStack;
  state().mqtt.tls = tls;
  state().mqtt.expectOverflow = expectOverflow;
}

void hwSetMqttHa(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.ha = enabled;
}

void hwSetMqttBuffer(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.bufferBytes = bytes;
}

void hwSetMqttMainStack(size_t bytes, size_t highWater) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqtt.mainStackBytes = bytes;
  state().mqtt.mainHighWater = highWater;
}

void hwSetBootDefaultsMarker(const std::string &marker) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().bootDefaultsMarker = marker;
  state().bootMarkerWritten = marker == "current";
}

void hwSetBootProfile(const std::string &profile) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().bootProfile = profile;
  state().psramMemtest = profile == "debug";
}

void hwSetPsramMemtest(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().psramMemtest = enabled;
}

void hwSetWifiFastConnect(const std::string &mode) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifiFastConnect = mode;
}

void hwSetWifiAuth(const std::string &auth) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifiAuth = auth;
}

void hwSetMqttDiscoveryPayload(size_t bytes) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().mqttDiscoveryPayload = bytes;
}

void hwStart(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();
  if (hw.started) {
    return;
  }
  hw.started = true;
  if (hw.profile.empty()) {
    hw.profile = defaultProfile();
    hw.caps = capsFor(hw.profile);
  }
  if (hw.pmic.variant.empty()) {
    hw.pmic.variant = hw.caps.pmic;
  }
  if (hw.pmic.gauge.empty()) {
    hw.pmic.gauge = hw.caps.gauge;
  }
  hw.pmic.batteryPresent = hw.caps.battery;
  hw.pmic.powerLatch = hw.caps.pmic[0] != 'n';
  hw.power.state = power_state_t::ON;
  hw.usb.mode = usb_mode_t::APP;

  if (!hw.psramExplicit) {
    const std::string mode = hw.caps.psram;
    if (mode == "octal" || mode == "quad") {
      const size_t bytes = 8u * 1024u * 1024u;
      heapSetPsram(mode, bytes, bytes / 2);
    }
  }

  ESP_LOGI(LOG_TAG, "profile %s mcu %s flash %u KiB psram %s pmic %s usb %s display %s",
           hw.profile.c_str(), hw.caps.mcu, static_cast<unsigned>(hw.caps.flashKb),
           heapPsramState(), hw.pmic.variant.c_str(), hw.caps.usb, hw.caps.display);
  if (heapPsramInitFailed()) {
    ESP_LOGE(LOG_TAG, "SPIRAM init failed; external allocations are unavailable");
  }

  mqtt_model_state_t &mqtt = hw.mqtt;
  if (mqtt.enabled) {
    // The main task high-water was 3,476 bytes before the MQTT path. The
    // hardware 8 KiB stack overflowed after MQTT was enabled, so model 8,500
    // bytes for the combined path and require the 12 KiB stack.
    if (mqtt.mainStackBytes < mqtt.mainHighWater) {
      mqtt.state = "main_overflow";
      mqtt.overflow = true;
      ESP_LOGE("mqtt", "***ERROR*** A stack overflow in task main");
    } else if (mqtt.ownerStack < 10240) {
      if (!mqtt.expectOverflow) {
        hw.failClosed = true;
        ESP_LOGE(LOG_TAG, "SIM FAIL: MQTT owner stack %u below 10240 bytes",
                 static_cast<unsigned>(mqtt.ownerStack));
        requestFailureExit();
      } else {
        mqtt.state = "overflow";
        mqtt.overflow = true;
        ESP_LOGE("mqtt", "***ERROR*** A stack overflow in task mqtt");
      }
    } else {
      const size_t ownerFreeFloor = mqtt.tls ? Furble::MQTTMemory::OWNER_TLS_INTERNAL_FREE_FLOOR
                                             : Furble::MQTTMemory::OWNER_INTERNAL_FREE_FLOOR;
      const size_t ownerLargestFloor = Furble::MQTTMemory::OWNER_INTERNAL_LARGEST_FLOOR;
      const size_t freeFloor = mqtt.tls ? Furble::MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR
                                        : Furble::MQTTMemory::START_INTERNAL_FREE_FLOOR;
      const size_t largestFloor = mqtt.tls ? Furble::MQTTMemory::START_TLS_INTERNAL_LARGEST_FLOOR
                                           : Furble::MQTTMemory::START_INTERNAL_LARGEST_FLOOR;
      if (heapFree() < ownerFreeFloor || heapLargest() < ownerLargestFloor) {
        mqtt.ownerGate = "closed";
        mqtt.state = "owner_blocked";
        ESP_LOGW("mqtt", "MQTT owner not created: internal free %u, largest %u (need %u/%u)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()),
                 static_cast<unsigned>(ownerFreeFloor), static_cast<unsigned>(ownerLargestFloor));
      } else if (heapAllocTagged(mqtt.ownerStack, "mqtt_owner", false, true) == 0) {
        mqtt.state = "alloc_failed";
        ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                 static_cast<unsigned>(mqtt.ownerStack));
      } else if (heapFree() < freeFloor || heapLargest() < largestFloor) {
        mqtt.startGate = "closed";
        mqtt.state = "start_blocked";
        ESP_LOGW("mqtt", "MQTT start deferred: internal free %u, largest %u (need %u/%u)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()),
                 static_cast<unsigned>(freeFloor), static_cast<unsigned>(largestFloor));
      } else {
        mqtt.ownerGate = "open";
        mqtt.startGate = "open";
        bool queueReady = false;
        if (heapPsramPresent() && heapPsramLargest() >= Furble::MQTTMemory::EVENT_QUEUE_BYTES) {
          const uint64_t queue =
              heapAllocTagged(Furble::MQTTMemory::EVENT_QUEUE_BYTES, "mqtt_queue", true, true);
          if (queue != 0) {
            mqtt.queueRegion = "psram";
            mqtt.queueBytes = Furble::MQTTMemory::EVENT_QUEUE_BYTES;
            queueReady = true;
          }
        }
        if (!queueReady) {
          if (heapAllocTagged(Furble::MQTTMemory::EVENT_QUEUE_BYTES, "mqtt_queue", false, true)
              == 0) {
            mqtt.state = "alloc_failed";
            ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                     static_cast<unsigned>(Furble::MQTTMemory::EVENT_QUEUE_BYTES));
            mqtt.heapFreeAfter = heapFree();
            return;
          }
          mqtt.queueRegion = "internal";
          mqtt.queueBytes = Furble::MQTTMemory::EVENT_QUEUE_BYTES;
        }
        if (heapAllocTagged(mqtt.workerStack, mqtt.tls ? "mqtt_tls_worker" : "mqtt_worker", false,
                            true)
            == 0) {
          mqtt.state = "alloc_failed";
          ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                   static_cast<unsigned>(mqtt.workerStack));
          mqtt.heapFreeAfter = heapFree();
          return;
        }
        for (int buffer = 0; buffer < 2; buffer++) {
          if (heapAllocTagged(mqtt.bufferBytes, "mqtt_buffer", false, true) == 0) {
            mqtt.state = "alloc_failed";
            ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                     static_cast<unsigned>(mqtt.bufferBytes));
            mqtt.heapFreeAfter = heapFree();
            return;
          }
        }
        // publishState(true) loads the saved camera list before discovery.
        constexpr size_t CAMERA_SNAPSHOT_BYTES = 5120;
        if (heapAllocTagged(CAMERA_SNAPSHOT_BYTES, "mqtt_camera", false, true) == 0) {
          mqtt.state = "alloc_failed";
          ESP_LOGE("heap", "Mem alloc fail. size 0x%04x caps 0x00000804",
                   static_cast<unsigned>(CAMERA_SNAPSHOT_BYTES));
          mqtt.heapFreeAfter = heapFree();
          return;
        }
        mqtt.cameraBytes = CAMERA_SNAPSHOT_BYTES;
        mqtt.state = "created";
        mqtt.discoveryFragments = static_cast<uint32_t>(
            (hw.mqttDiscoveryPayload + mqtt.bufferBytes - 1) / mqtt.bufferBytes);
        ESP_LOGI("mqtt", "owner stack %u worker stack %u%s queue %s created",
                 static_cast<unsigned>(mqtt.ownerStack), static_cast<unsigned>(mqtt.workerStack),
                 mqtt.tls ? " TLS" : "", mqtt.queueRegion.c_str());
      }
    }
    if (mqtt.bufferBytes > 2048) {
      hw.failClosed = true;
      ESP_LOGE(LOG_TAG, "SIM FAIL: MQTT buffer %u above 2048 bytes",
               static_cast<unsigned>(mqtt.bufferBytes));
      requestFailureExit();
    }
    if (mqtt.ha && mqtt.state == "created") {
      if (heapFree() < 10240 || heapLargest() < 4096) {
        mqtt.discoveryGate = "closed";
        mqtt.haState = "blocked";
        ESP_LOGW("mqtt", "HA discovery skipped: internal free %u, largest %u (need 10240/4096)",
                 static_cast<unsigned>(heapFree()), static_cast<unsigned>(heapLargest()));
      } else {
        mqtt.discoveryGate = "open";
        mqtt.haCache = "valid";
        const size_t discovery = 8192;
        const uint64_t discoveryId = heapAllocTagged(discovery, "mqtt_ha_discovery", false, false);
        if (discoveryId == 0) {
          mqtt.haState = "failed";
          ESP_LOGE("mqtt", "Home Assistant discovery allocation failed; discovery disabled");
        } else {
          mqtt.haState = "ok";
          mqtt.haPeak = discovery;
          heapFreeTagged(discoveryId);
        }
      }
    }
    mqtt.heapFreeAfter = heapFree();
  }
}

void hwSetResetReason(uint32_t reason) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().resetReason = reason;
  state().resetReasonName = reasonName(reason);
}

uint32_t hwResetReason(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().resetReason;
}

bool hwAction(const std::string &name) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  hardware_state_t &hw = state();

  if (name == "power-single-click") {
    hw.power.clicks++;
    if (hw.power.state == power_state_t::OFF) {
      powerOnLocked(hw);
      ESP_LOGI(LOG_TAG, "power button single click: power on");
    } else if (hw.power.state == power_state_t::DOWNLOAD || hw.usb.mode != usb_mode_t::APP) {
      recoverToAppLocked(hw, "power button single click recovered ROM download mode");
    } else {
      hw.power.resets++;
      hw.resetReason = ESP_RST_SW;
      hw.resetReasonName = "software";
      ESP_LOGI(LOG_TAG, "power button single click: reset");
    }
    return true;
  }
  if (name == "power-double-click") {
    hw.power.doubleClicks++;
    if (hw.power.state == power_state_t::ON) {
      hw.power.state = power_state_t::OFF;
      hw.power.offs++;
      notePowerOff();
      ESP_LOGI(LOG_TAG, "power button double click: power off");
    } else {
      powerOnLocked(hw);
      ESP_LOGI(LOG_TAG, "power button double click: power on");
    }
    return true;
  }
  if (name == "power-long-press") {
    hw.power.longPresses++;
    if (hw.power.state == power_state_t::ON) {
      hw.power.downloads++;
      enterRomLocked(hw, "power button long press reached download mode");
    }
    return true;
  }
  if (name == "usb-open") {
    hw.usb.hostOpen = true;
    ESP_LOGI(LOG_TAG, "USB host opened the Serial/JTAG port");
    return true;
  }
  if (name == "usb-close") {
    hw.usb.hostOpen = false;
    ESP_LOGI(LOG_TAG, "USB host closed the Serial/JTAG port; mode stays %s",
             hw.usb.mode == usb_mode_t::APP ? "app" : "rom");
    return true;
  }
  if (name == "usb-dtr") {
    hw.usb.dtr = true;
    return true;
  }
  if (name == "usb-rts") {
    hw.usb.rts = true;
    if (hw.usb.hostOpen && hw.usb.dtr) {
      enterRomLocked(hw, "USB host open with DTR/RTS asserted");
    }
    return true;
  }
  if (name == "usb-reset") {
    if (hw.usb.mode == usb_mode_t::APP) {
      hw.resetReason = ESP_RST_USB;
      hw.resetReasonName = "usb_uart_chip_reset";
    } else {
      recoverToAppLocked(hw, "USB host reset recovered the application");
    }
    return true;
  }
  if (name == "usb-rom-entry") {
    enterRomLocked(hw, "ROM download mode forced by scenario");
    return true;
  }
  if (name == "usb-esptool-sync") {
    if (hw.usb.mode == usb_mode_t::ROM) {
      hw.usb.mode = usb_mode_t::ESPTOOL;
      hw.usb.syncs++;
      ESP_LOGI(LOG_TAG, "esptool sync accepted; application console and BLE remain silent");
    }
    return true;
  }
  if (name == "pmic-charge-on") {
    hw.pmic.chargeEnabled = true;
    hw.pmic.charging = true;
    hw.pmic.chargeCurrentMa = 100;
    return true;
  }
  if (name == "pmic-charge-off") {
    hw.pmic.chargeEnabled = false;
    hw.pmic.charging = false;
    hw.pmic.chargeCurrentMa = 0;
    return true;
  }
  if (name == "pmic-battery-present") {
    hw.pmic.batteryPresent = true;
    hw.pmic.batteryPercent = 80;
    hw.pmic.batteryMillivolts = 4000;
    hw.pmic.charging = false;
    return true;
  }
  if (name == "pmic-battery-absent") {
    hw.pmic.batteryPresent = false;
    hw.pmic.batteryPercent = 0;
    hw.pmic.batteryMillivolts = 0;
    hw.pmic.charging = false;
    return true;
  }
  if (name == "pmic-brownout") {
    hw.pmic.brownouts++;
    hw.resetReason = ESP_RST_BROWNOUT;
    hw.resetReasonName = "brownout";
    hw.power.state = power_state_t::OFF;
    ESP_LOGE(LOG_TAG, "PMIC brownout detected; rail dropped");
    crashLogInject(ESP_RST_BROWNOUT, "pmic", "PMIC brownout detected; rail dropped");
    return true;
  }
  if (name == "pmic-watchdog-on") {
    hw.pmic.watchdogEnabled = true;
    return true;
  }
  if (name == "pmic-watchdog-off") {
    hw.pmic.watchdogEnabled = false;
    return true;
  }
  if (name == "wifi-connect") {
    hw.wifi.state = "connected";
    hw.wifi.reason = "none";
    hw.wifi.attempts = 0;
    hw.wifi.retryMs = 0;
    return true;
  }
  if (name == "wifi-disconnect") {
    hw.wifi.state = "idle";
    hw.wifi.reason = "user";
    return true;
  }
  if (name == "wifi-fail-auth" || name == "wifi-fail-notfound" || name == "wifi-fail-timeout") {
    hw.wifi.state = "failed";
    hw.wifi.reason = (name == "wifi-fail-auth")
                         ? "auth"
                         : ((name == "wifi-fail-notfound") ? "notfound" : "timeout");
    hw.wifi.attempts++;
    hw.wifi.retryMs = std::min<uint32_t>(30000, 1000U << std::min<uint32_t>(hw.wifi.attempts, 5));
    return true;
  }
  if (name == "wifi-ntp-sync") {
    hw.wifi.ntpSynced = true;
    hw.wifi.ntpOffsetUs = 0;
    return true;
  }
  if (name == "wifi-ntp-fail") {
    hw.wifi.ntpSynced = false;
    hw.wifi.ntpOffsetUs = 1200000;
    return true;
  }
  if (name == "ethernet-up") {
    hw.net.ethernet = "up";
    hw.net.linkUps++;
    return true;
  }
  if (name == "ethernet-down") {
    hw.net.ethernet = "down";
    hw.net.linkDowns++;
    return true;
  }
  if (name == "ble-auth-timeout") {
    hw.ble.auth = "timeout";
    hw.ble.companion = "reauth";
    hw.ble.authTimeouts++;
    ESP_LOGW(LOG_TAG, "companion auth timeout; link stays up for reauth");
    return true;
  }
  if (name == "ble-gatt-stale") {
    hw.ble.gattCache = "stale";
    return true;
  }
  if (name == "ble-gatt-fresh") {
    hw.ble.gattCache = "valid";
    return true;
  }
  if (name == "companion-reidentify") {
    hw.ble.reidentifyCount++;
    hw.ble.bond = "none";
    hw.ble.gattCache = "valid";
    hw.ble.companion = "advertising";
    ESP_LOGI(LOG_TAG, "companion reidentify: address rotated, bond dropped, pairing reopened");
    return true;
  }
  if (name == "companion-reject") {
    hw.ble.rejectedLinks++;
    hw.ble.companion = "rejected";
    ESP_LOGW(LOG_TAG, "companion rejected an unbonded central");
    return true;
  }
  if (name == "companion-reconnect") {
    hw.ble.reconnects++;
    hw.ble.companion = "connected";
    hw.ble.auth = "ok";
    return true;
  }
  if (name == "audio-overcurrent") {
    hw.peripheral.audio = "overcurrent";
    hw.peripheral.audioOvercurrents++;
    ESP_LOGE(LOG_TAG, "speaker overcurrent; output disabled");
    return true;
  }
  if (name == "ir-timeout") {
    hw.peripheral.ir = "timeout";
    hw.peripheral.irTimeouts++;
    ESP_LOGW(LOG_TAG, "IR RMT transmit timeout");
    return true;
  }
  if (name == "sd-present") {
    hw.peripheral.sd = "mounted";
    return true;
  }
  if (name == "sd-absent") {
    hw.peripheral.sd = "absent";
    return true;
  }
  if (name == "sd-error") {
    hw.peripheral.sd = "error";
    hw.peripheral.sdErrors++;
    return true;
  }
  if (name == "companion-enable") {
    hw.companionEnabled = true;
    hw.ble.companion = "advertising";
    return true;
  }
  if (name == "companion-disable") {
    hw.companionEnabled = false;
    hw.ble.companion = "off";
    return true;
  }
  if (name == "boot-reboot") {
    hw.bootSeedAttempts++;
    if (hw.bootDefaultsMarker != "current" || hw.bootSeedFail) {
      hw.bootDefaultScans++;
      if (hw.bootSeedFail) {
        hw.bootMarkerWritten = false;
        ESP_LOGW(LOG_TAG, "settings default seed failed; marker not written, next boot rescans");
      } else {
        hw.bootMarkerWritten = true;
        hw.bootDefaultsMarker = "current";
      }
    }
    return true;
  }
  if (name == "boot-seed-fail") {
    hw.bootSeedFail = true;
    return true;
  }
  if (name == "wifi-fast-fail") {
    hw.wifiDirectFailures++;
    hw.wifiMovedAp = true;
    if (hw.wifiScans == 0) {
      hw.wifiScans++;
      ESP_LOGW(LOG_TAG, "fast connect failed; scanning before the next join");
    }
    return true;
  }
  if (name == "wifi-moved-ap") {
    hw.wifiMovedAp = true;
    hw.wifiFastConnect = "unusable";
    return true;
  }
  if (name == "mqtt-ha-toggle") {
    hw.mqtt.haCache = "invalid";
    return true;
  }
  if (name == "mqtt-discovery-payload") {
    hw.mqttDiscoveryPayload = 3005;
    hw.mqttDiscoveryFragments = static_cast<uint32_t>((hw.mqttDiscoveryPayload + 1535) / 1536);
    return true;
  }
  if (name == "ota-update") {
    hw.otaSlot = hw.otaSlot == "ota_0" ? "ota_1" : "ota_0";
    hw.otaRollback = true;
    hw.otaUpdates++;
    ESP_LOGI(LOG_TAG, "OTA image written to %s; rollback armed", hw.otaSlot.c_str());
    return true;
  }
  if (name == "ota-commit") {
    hw.otaRollback = false;
    hw.otaCommits++;
    ESP_LOGI(LOG_TAG, "OTA image in %s marked valid", hw.otaSlot.c_str());
    return true;
  }
  if (name == "ota-rollback") {
    if (hw.otaRollback) {
      hw.otaSlot = hw.otaSlot == "ota_0" ? "ota_1" : "ota_0";
      hw.otaRollback = false;
      ESP_LOGW(LOG_TAG, "OTA rollback selected %s", hw.otaSlot.c_str());
    }
    return true;
  }
  if (name == "alloc-icon") {
    const size_t bytes = 12291;
    if (heapPsramPresent()) {
      const uint64_t id = heapAllocTagged(bytes, "icon_decode", true, false);
      hw.iconState = id == 0 ? "failed" : "ok";
      if (id != 0) {
        heapFreeTagged(id);
      }
    } else {
      const uint64_t id = heapAllocTagged(bytes, "icon_decode", false, false);
      hw.iconState = id == 0 ? "failed" : "ok";
      if (id != 0) {
        heapFreeTagged(id);
      } else {
        ESP_LOGE("heap", "Mem alloc fail. size 0x3003 caps 0x00001800");
      }
    }
    return true;
  }
  if (name == "heap-leak-probe") {
    if (hw.leakProbe == 0) {
      hw.leakProbe = heapAllocTagged(512, "leak_probe", false, false);
    }
    return true;
  }
  if (name == "heap-leak-free") {
    if (hw.leakProbe != 0) {
      heapFreeTagged(hw.leakProbe);
      hw.leakProbe = 0;
    }
    return true;
  }
  if (name == "psram-task-start") {
    if (std::strcmp(heapTaskStackRegion(), "psram") == 0 && heapPsramPresent()) {
      xTaskCreate(psramProbeTask, "psram_probe", 2048, nullptr, 1, nullptr);
    }
    return true;
  }
  if (name == "flash-write") {
    heapFlashWrite();
    if (heapFlashUnsafe()) {
      hw.failClosed = true;
      requestFailureExit();
    }
    return true;
  }
  return false;
}

std::string hwQuery(const std::string &key) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const hardware_state_t &hw = state();
  const auto prefixed = [&key](const char *prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return key.size() >= length && key.compare(0, length, prefix) == 0;
  };

  if (prefixed("hw.")) {
    const std::string sub = key.substr(3);
    if (sub == "profile") {
      return hw.profile;
    }
    if (sub == "mcu") {
      return hw.caps.mcu;
    }
    if (sub == "flash_kb") {
      return std::to_string(hw.caps.flashKb);
    }
    if (sub == "battery") {
      return hw.caps.battery ? "present" : "absent";
    }
    if (sub == "psram") {
      return heapPsramMode();
    }
    if (sub == "psram_state") {
      return heapPsramState();
    }
    if (sub == "pmic") {
      return hw.pmic.variant;
    }
    if (sub == "gauge") {
      return hw.pmic.gauge;
    }
    if (sub == "usb") {
      return hw.caps.usb;
    }
    if (sub == "display") {
      return hw.caps.display;
    }
    if (sub == "touch") {
      return hw.caps.touch ? "yes" : "no";
    }
    if (sub == "wifi") {
      return hw.caps.wifi ? "yes" : "no";
    }
    if (sub == "ethernet") {
      return hw.caps.ethernet ? "yes" : "no";
    }
    if (sub == "ble") {
      return hw.caps.ble ? "yes" : "no";
    }
    if (sub == "ir") {
      return hw.caps.ir ? "yes" : "no";
    }
    if (sub == "sd") {
      return hw.caps.sd ? "yes" : "no";
    }
    if (sub == "audio") {
      return hw.caps.audio ? "yes" : "no";
    }
    if (sub == "rtc") {
      return hw.caps.rtc ? "yes" : "no";
    }
  }
  if (prefixed("pmic.")) {
    const std::string sub = key.substr(5);
    if (sub == "variant") {
      return hw.pmic.variant;
    }
    if (sub == "gauge") {
      return hw.pmic.gauge;
    }
    if (sub == "battery") {
      return hw.pmic.batteryPresent ? "present" : "absent";
    }
    if (sub == "charging") {
      return hw.pmic.charging ? "charging" : "idle";
    }
    if (sub == "charger") {
      return hw.pmic.chargeEnabled ? "on" : "off";
    }
    if (sub == "charge_current_ma") {
      return std::to_string(hw.pmic.chargeCurrentMa);
    }
    if (sub == "watchdog") {
      if (hw.pmic.variant == "m5pm1") {
        return hw.pmic.watchdogEnabled ? "armed" : "disabled";
      }
      return "unavailable";
    }
    if (sub == "watchdog_feeds") {
      return std::to_string(hw.pmic.watchdogFeeds);
    }
    if (sub == "download_lock") {
      return hw.pmic.downloadLocked ? "locked" : "unlocked";
    }
    if (sub == "power_latch") {
      return hw.pmic.powerLatch ? "latched" : "none";
    }
    if (sub == "brownouts") {
      return std::to_string(hw.pmic.brownouts);
    }
    if (sub == "battery_mv") {
      return std::to_string(hw.pmic.batteryMillivolts);
    }
    if (sub == "battery_percent") {
      return std::to_string(hw.pmic.batteryPercent);
    }
  }
  if (prefixed("power.")) {
    const std::string sub = key.substr(6);
    if (sub == "state") {
      return hw.power.state == power_state_t::ON    ? "on"
             : hw.power.state == power_state_t::OFF ? "off"
                                                    : "download";
    }
    if (sub == "clicks") {
      return std::to_string(hw.power.clicks);
    }
    if (sub == "double_clicks") {
      return std::to_string(hw.power.doubleClicks);
    }
    if (sub == "long_presses") {
      return std::to_string(hw.power.longPresses);
    }
    if (sub == "resets") {
      return std::to_string(hw.power.resets);
    }
    if (sub == "offs") {
      return std::to_string(hw.power.offs);
    }
    if (sub == "downloads") {
      return std::to_string(hw.power.downloads);
    }
    if (sub == "recoveries") {
      return std::to_string(hw.power.recoveries);
    }
  }
  if (prefixed("usb.")) {
    const std::string sub = key.substr(4);
    if (sub == "transport") {
      return hw.caps.usb;
    }
    if (sub == "mode") {
      return hw.usb.mode == usb_mode_t::APP   ? "app"
             : hw.usb.mode == usb_mode_t::ROM ? "rom"
                                              : "esptool";
    }
    if (sub == "host") {
      return hw.usb.hostOpen ? "open" : "closed";
    }
    if (sub == "console") {
      return (hw.usb.mode == usb_mode_t::APP && hw.power.state != power_state_t::OFF) ? "yes"
                                                                                      : "no";
    }
    if (sub == "ble") {
      return (hw.usb.mode == usb_mode_t::APP && hw.power.state != power_state_t::OFF && hw.caps.ble)
                 ? "yes"
                 : "no";
    }
    if (sub == "rom_entries") {
      return std::to_string(hw.usb.romEntries);
    }
    if (sub == "syncs") {
      return std::to_string(hw.usb.syncs);
    }
    if (sub == "recoveries") {
      return std::to_string(hw.usb.recoveries);
    }
  }
  if (prefixed("wifi.")) {
    const std::string sub = key.substr(5);
    if (sub == "state") {
      return hw.wifi.state;
    }
    if (sub == "reason") {
      return hw.wifi.reason;
    }
    if (sub == "ip") {
      return hw.wifi.ip;
    }
    if (sub == "ssid") {
      return hw.wifi.ssid;
    }
    if (sub == "rssi") {
      return std::to_string(hw.wifi.rssi);
    }
    if (sub == "attempts") {
      return std::to_string(hw.wifi.attempts);
    }
    if (sub == "retry_ms") {
      return std::to_string(hw.wifi.retryMs);
    }
    if (sub == "ntp") {
      return hw.wifi.ntpSynced ? "synced" : "idle";
    }
    if (sub == "ntp_offset_us") {
      return std::to_string(hw.wifi.ntpOffsetUs);
    }
  }
  if (key == "net.ethernet") {
    return hw.net.ethernet;
  }
  if (key == "net.link_ups") {
    return std::to_string(hw.net.linkUps);
  }
  if (key == "net.link_downs") {
    return std::to_string(hw.net.linkDowns);
  }
  if (prefixed("ble.")) {
    const std::string sub = key.substr(4);
    if (sub == "bond") {
      return hw.ble.bond;
    }
    if (sub == "auth") {
      return hw.ble.auth;
    }
    if (sub == "gatt_cache") {
      return hw.ble.gattCache;
    }
    if (sub == "auth_timeouts") {
      return std::to_string(hw.ble.authTimeouts);
    }
    if (sub == "reconnects") {
      return std::to_string(hw.ble.reconnects);
    }
  }
  if (prefixed("companion.")) {
    const std::string sub = key.substr(10);
    if (sub == "enabled") {
      return hw.companionEnabled ? "yes" : "no";
    }
    if (sub == "state") {
      return hw.ble.companion;
    }
    if (sub == "reidentify") {
      return std::to_string(hw.ble.reidentifyCount);
    }
    if (sub == "rejected") {
      return std::to_string(hw.ble.rejectedLinks);
    }
  }
  if (prefixed("audio.")) {
    const std::string sub = key.substr(6);
    if (sub == "state") {
      return hw.peripheral.audio;
    }
    if (sub == "overcurrents") {
      return std::to_string(hw.peripheral.audioOvercurrents);
    }
  }
  if (prefixed("ir.")) {
    const std::string sub = key.substr(3);
    if (sub == "state") {
      return hw.peripheral.ir;
    }
    if (sub == "timeouts") {
      return std::to_string(hw.peripheral.irTimeouts);
    }
  }
  if (prefixed("sd.")) {
    const std::string sub = key.substr(3);
    if (sub == "state") {
      return hw.peripheral.sd;
    }
    if (sub == "errors") {
      return std::to_string(hw.peripheral.sdErrors);
    }
  }
  if (key == "platform.reset_reason") {
    return hw.resetReasonName;
  }
  if (key == "boot.defaults_marker") {
    return hw.bootDefaultsMarker;
  }
  if (key == "boot.default_scans") {
    return std::to_string(hw.bootDefaultScans);
  }
  if (key == "boot.seed_attempts") {
    return std::to_string(hw.bootSeedAttempts);
  }
  if (key == "boot.marker_written") {
    return hw.bootMarkerWritten ? "yes" : "no";
  }
  if (key == "boot.psram_memtest") {
    return hw.psramMemtest ? "on" : "off";
  }
  if (key == "boot.profile") {
    return hw.bootProfile;
  }
  if (key == "boot.reset_to_app_ms") {
    return std::to_string(hw.resetToAppMs);
  }
  if (key == "wifi.fast_connect") {
    return hw.wifiFastConnect;
  }
  if (key == "wifi.auth") {
    return hw.wifiAuth;
  }
  if (key == "wifi.direct_failures") {
    return std::to_string(hw.wifiDirectFailures);
  }
  if (key == "wifi.scans") {
    return std::to_string(hw.wifiScans);
  }
  if (key == "wifi.moved_ap") {
    return hw.wifiMovedAp ? "yes" : "no";
  }
  if (key == "ota.slot") {
    return hw.otaSlot;
  }
  if (key == "ota.rollback") {
    return hw.otaRollback ? "yes" : "no";
  }
  if (key == "ota.updates") {
    return std::to_string(hw.otaUpdates);
  }
  if (key == "ota.commits") {
    return std::to_string(hw.otaCommits);
  }
  if (key == "alloc.icon") {
    return hw.iconState;
  }
  if (prefixed("mqttmodel.")) {
    const std::string sub = key.substr(10);
    if (sub == "state") {
      return hw.mqtt.state;
    }
    if (sub == "overflow") {
      return hw.mqtt.overflow ? "yes" : "no";
    }
    if (sub == "owner_stack") {
      return std::to_string(hw.mqtt.ownerStack);
    }
    if (sub == "worker_stack") {
      return std::to_string(hw.mqtt.workerStack);
    }
    if (sub == "tls") {
      return hw.mqtt.tls ? "yes" : "no";
    }
    if (sub == "ha") {
      return hw.mqtt.haState;
    }
    if (sub == "buffer") {
      return std::to_string(hw.mqtt.bufferBytes);
    }
    if (sub == "ha_peak") {
      return std::to_string(hw.mqtt.haPeak);
    }
    if (sub == "owner_gate") {
      return hw.mqtt.ownerGate;
    }
    if (sub == "start_gate") {
      return hw.mqtt.startGate;
    }
    if (sub == "discovery_gate") {
      return hw.mqtt.discoveryGate;
    }
    if (sub == "queue_bytes") {
      return std::to_string(hw.mqtt.queueBytes);
    }
    if (sub == "queue_region") {
      return hw.mqtt.queueRegion;
    }
    if (sub == "camera_bytes") {
      return std::to_string(hw.mqtt.cameraBytes);
    }
    if (sub == "main_stack") {
      return std::to_string(hw.mqtt.mainStackBytes);
    }
    if (sub == "main_high_water") {
      return std::to_string(hw.mqtt.mainHighWater);
    }
    if (sub == "main_stack_ok") {
      return hw.mqtt.mainStackBytes >= hw.mqtt.mainHighWater ? "yes" : "no";
    }
    if (sub == "ha_cache") {
      return hw.mqtt.haCache;
    }
    if (sub == "discovery_fragments") {
      return std::to_string(hw.mqtt.discoveryFragments);
    }
    if (sub == "heap_free_after") {
      return std::to_string(hw.mqtt.heapFreeAfter);
    }
  }
  return "";
}

bool hwFailClosed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().failClosed;
}

bool hwBatteryPresent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPresent;
}

bool hwGaugeLevel(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPresent && state().pmic.gauge != "none";
}

bool hwGaugeVoltage(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent
         && (gauge == "voltage" || gauge == "voltage_current" || gauge == "voltage_charge");
}

bool hwGaugeCurrent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent && (gauge == "voltage_current");
}

bool hwGaugeCharge(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const std::string &gauge = state().pmic.gauge;
  return state().pmic.batteryPresent
         && (gauge == "voltage_current" || gauge == "voltage_charge" || gauge == "four_level");
}

int hwBatteryPercent(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryPercent;
}

int hwBatteryMillivolts(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.batteryMillivolts;
}

int hwBatteryCurrentMa(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.chargeCurrentMa;
}

bool hwBatteryCharging(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.charging;
}

void hwWifiSetConnected(bool connected) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().wifi.state = connected ? "connected" : "disabled";
  state().wifi.reason = connected ? "none" : "user";
  if (!connected) {
    state().wifi.attempts = 0;
    state().wifi.retryMs = 0;
  }
}

bool hwWifiConnected(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.state == "connected";
}

const char *hwWifiState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.state.c_str();
}

const char *hwWifiReason(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.reason.c_str();
}

const char *hwWifiIp(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ip.c_str();
}

const char *hwWifiSsid(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ssid.c_str();
}

int hwWifiRssi(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.rssi;
}

uint32_t hwWifiAttempts(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.attempts;
}

uint32_t hwWifiRetryMs(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.retryMs;
}

bool hwNtpSynced(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().wifi.ntpSynced;
}

const char *hwMqttModelState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.state.c_str();
}

bool hwMqttModelOverflow(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.overflow;
}

size_t hwMqttOwnerStack(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.ownerStack;
}

size_t hwMqttWorkerStack(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.workerStack;
}

const char *hwMqttHaState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.haState.c_str();
}

size_t hwMqttBufferBytes(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().mqtt.bufferBytes;
}

void hwSetWatchdogEnabled(bool enabled) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.watchdogEnabled = enabled;
  if (enabled) {
    state().pmic.watchdogFeeds = 0;
  }
}

void hwSetDownloadLocked(bool locked) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  state().pmic.downloadLocked = locked;
}

uint32_t hwWatchdogFeeds(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().pmic.watchdogFeeds;
}

void hwNoteWatchdogFeed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  if (state().pmic.watchdogEnabled) {
    state().pmic.watchdogFeeds++;
  }
}

}  // namespace Furble::Sim

extern "C" uint32_t furble_sim_hw_reset_reason(void) {
  return Furble::Sim::hwResetReason();
}
