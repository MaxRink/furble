// Headless production service-loop contract.
//
// src/main.cpp is compiled unchanged with FURBLE_NO_DISPLAY. The production
// app_main boot order, UI request queue, vUITask cadence and 1 Hz GPS service
// run without linking SDL, M5GFX or a panel. The modules it drives are host
// doubles, so each boot service and every loop-owned call is observable.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "FurbleBootScreen.h"
#include "FurblePlatform.h"
#include "FurbleUI.h"
#include "M5Unified.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "headless_state.h"

#if defined(FURBLE_HOST_COVERAGE)
// The clang profile runtime writes the raw profile from an exit handler, and
// std::_Exit skips exit handlers. Flush it explicitly before leaving.
extern "C" int __llvm_profile_write_file(void);
#endif

extern "C" void app_main(void);

namespace {

constexpr int REQUIRED_LOOPS = 45;
int g_Loops = 0;
int g_Failures = 0;
// "watchdog-refused" runs the same production boot with the PMIC double
// refusing the arm, which is the degraded state the boot log must show.
bool g_WatchdogRefused = false;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "HEADLESS FAIL: " << message << '\n';
    g_Failures++;
  }
}

// The boundary log shim stores every ESP_LOGx line under its own lock.
bool logContains(const char *needle) {
  auto &store = BoundaryHost::detail::logStore();
  const std::lock_guard<std::mutex> lock(store.mutex);
  for (const auto &line : store.lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> bootWithoutTask() {
  const std::lock_guard<std::mutex> lock(HeadlessState::bootMutex);
  std::vector<std::string> filtered;
  for (const auto &stage : HeadlessState::boot) {
    if (stage != "control.task") {
      filtered.push_back(stage);
    }
  }
  return filtered;
}

// The service loop never returns, so the test exits from inside it.
[[noreturn]] void exitNow(int code) {
  std::cout.flush();
#if defined(FURBLE_HOST_COVERAGE)
  __llvm_profile_write_file();
#endif
  std::_Exit(code);
}

void finish(void) {
  using namespace HeadlessState;

  // Join the production control task before observing its counter. The
  // counter is atomic, but a scheduling delay could otherwise let this
  // assertion run before the task has incremented it.
  furbleHostStopTasks();

  const std::vector<std::string> expected = {
      "crashlog.init", "netif.init",     "eventloop.init", "platform.init", "timekeeper.init",
      "boot.begin",    "ir.init",        "Infrared",       "feedback.init", "Feedback",
      "sd.init",       "Storage",        "Power",          "device.init",   "Bluetooth",
      "webui.early",   "companion.init", "Companion",      "mqtt.init",     "ethernet.init",
      "Ethernet",      "wifi.init",      "gps.init",       "webui.init",    "console.init",
      "boot.finish",
  };
  const auto actual = bootWithoutTask();
  check(actual == expected, "the headless boot order changed");

  check(Furble::BootScreen::stepsShown() == 7, "the headless boot advanced the wrong stage count");
  check(controlTasks == 1, "the production Control task was not created exactly once");
  check(cpuFreqSets == 1, "the stored CPU frequency was not applied");
  check(watchdogEnables == 1, "the StickS3 watchdog setting was not applied");
  if (g_WatchdogRefused) {
    // A refused arm must be logged and must not end the boot: every stage
    // below has already run by the time finish() is reached.
    check(watchdogRefusals == 1, "the refused watchdog arm was not observed exactly once");
    check(logContains("M5PM1 watchdog arm refused"), "a refused watchdog arm was not logged");
  } else {
    check(watchdogRefusals == 0, "the watchdog double refused an arm it should have taken");
    check(!logContains("M5PM1 watchdog arm refused"),
          "the watchdog refusal was logged without a refusal");
  }
  check(consoleQueued, "the console did not queue its headless requests");
  check(irInit == 1, "IR::init was not called once");
  check(sdInit == 1, "SD::init was not called once");
  check(feedbackSignals >= 1, "the queued feedback test never reached Feedback::signal");
  check(feedbackReloads >= 1, "the queued feedback reload never reached Feedback::reload");
  check(gpsReloads >= 1, "the queued GPS reload never reached GPS::reloadSetting");
  check(wifiInit == 1, "WiFi::init was not called once");
  check(gpsInit == 1, "GPS::init was not called once");
  check(bleInit == 1, "the companion/BLE service was not initialized");
  check(webuiEarly == 1 && webuiInit == 1, "the WebUI was not initialized in both phases");
  check(mqttInit == 1, "MQTT was not initialized");
  check(ethernetInit == 1, "Ethernet was not initialized");
  check(platformUpdates >= static_cast<uint32_t>(REQUIRED_LOOPS),
        "the headless vUITask stopped pumping Platform::update");
  check(crashUpdates >= static_cast<uint32_t>(REQUIRED_LOOPS),
        "the headless vUITask stopped pumping CrashLog::update");
  check(pressureTicks >= static_cast<uint32_t>(REQUIRED_LOOPS),
        "the headless vUITask stopped ticking the memory pressure governor");
  check(scanProcessed >= static_cast<uint32_t>(REQUIRED_LOOPS),
        "the headless vUITask stopped draining Scan callbacks");
  check(gpsUpdates >= 2, "the headless vUITask did not service GPS at 1 Hz");
  check(!M5.Display.panelUsed(), "the headless service loop touched an SDL/display panel");

  for (size_t index = 0; index < actual.size(); index++) {
    std::cout << "headless.boot[" << index << "]=" << actual[index] << '\n';
  }
  std::cout << "headless.loop platform=" << platformUpdates << " crash=" << crashUpdates
            << " scan=" << scanProcessed << " gps=" << gpsUpdates << '\n';
  std::cout << "headless.services console=" << consoleInit << " ir=" << irInit << " sd=" << sdInit
            << " wifi=" << wifiInit << " gps=" << gpsInit << " ble=" << bleInit
            << " webui=" << webuiInit << " mqtt=" << mqttInit << " ethernet=" << ethernetInit
            << '\n';
  std::cout << "headless.no_panel touched=" << (M5.Display.panelUsed() ? 1 : 0) << '\n';

  if (g_Failures != 0) {
    exitNow(1);
  }
  std::cout << "headless_service_test: PASS\n";
  exitNow(0);
}

void delayHook(void) {
  HeadlessState::nowUs += 50 * 1000;
  g_Loops++;
  if (g_Loops >= REQUIRED_LOOPS) {
    finish();
  }
}

}  // namespace

int main(int argc, char **argv) {
  if ((argc > 1) && (std::string(argv[1]) == "watchdog-refused")) {
    g_WatchdogRefused = true;
    HeadlessState::watchdogEnableResult.store(false);
  }
  furbleHostSetDelayHook(delayHook);
  app_main();
  std::cerr << "app_main returned unexpectedly\n";
  return 1;
}
