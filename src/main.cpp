#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_event.h>
#include <esp_netif.h>
#include <algorithm>
#include <memory>

#if defined(FURBLE_NO_DISPLAY)
#include <M5Unified.h>
#include <esp_timer.h>
#endif

#include "Device.h"
#include "Scan.h"

#include "FurbleBatteryStatus.h"
#include "FurbleBootScreen.h"
#include "FurbleBootTimeline.h"
#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleCrashLog.h"
#include "FurbleFeedback.h"
#include "FurbleMemoryPressure.h"
#if defined(FURBLE_ETHERNET)
#include "FurbleEthernet.h"
#endif
#if defined(FURBLE_NO_DISPLAY)
#include "FurbleGPS.h"
#include "FurbleHeadlessPolicy.h"
#endif
#include "FurbleIR.h"
#if defined(FURBLE_MQTT) && FURBLE_MQTT
#include "FurbleMQTT.h"
#endif
#include "FurblePlatform.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleTimezone.h"
#include "FurbleTimezoneGPS.h"
#include "FurbleUI.h"
#include "FurbleWiFi.h"
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
#include "FurbleWebUI.h"
#endif
#include "protocol/CameraListProtocol.h"

#if defined(FURBLE_NO_DISPLAY)
namespace Furble {

imu_mutex_t g_IMUMutex;

// Device status for the companion service. The display build serves these from
// the UI task, the headless build has no UI so it reads M5.Power directly and
// reports an idle intervalometer.
int32_t UI::getBatteryLevel(void) {
  const auto &caps = Platform::getInstance().getBatteryCaps();
  return BatteryStatus::level(caps.level, M5.Power.getBatteryLevel());
}

int16_t UI::getBatteryVoltage(void) {
  const auto &caps = Platform::getInstance().getBatteryCaps();
  return BatteryStatus::voltage(caps.voltage, M5.Power.getBatteryVoltage());
}

int32_t UI::getBatteryCurrent(void) {
  const auto &caps = Platform::getInstance().getBatteryCaps();
  return BatteryStatus::current(caps.current, M5.Power.getBatteryCurrent());
}

int16_t UI::getBatteryVBUSVoltage(void) {
#if defined(FURBLE_WAVESHARE_S3_ETH)
  // The board exposes no software-readable VBUS/PoE telemetry.
  return BatteryStatus::vbus(false, 0);
#else
  return BatteryStatus::vbus(true, M5.Power.getVBUSVoltage());
#endif
}

bool UI::isBatteryCharging(void) {
  const auto &caps = Platform::getInstance().getBatteryCaps();
  return BatteryStatus::charging(caps.charging, static_cast<int>(M5.Power.isCharging()) == 1);
}

uint8_t UI::getIntervalometerState(void) {
  return 0;
}

uint16_t UI::getIntervalometerRemaining(void) {
  return 0;
}

}  // namespace Furble
#endif

#if defined(FURBLE_NO_DISPLAY)
namespace Furble {
namespace {

constexpr UBaseType_t HEADLESS_REQUEST_QUEUE_LENGTH = 8;

typedef struct {
  UI::Request request;
  int32_t arg;
#if defined(FURBLE_CONSOLE)
  RequestState *state;
#endif
} headless_request_t;

QueueHandle_t g_HeadlessRequestQueue = NULL;

void printCameras(bool reload) {
  if (reload) {
    CameraList::load();
  }

  printf("saved: %u\n", static_cast<unsigned>(CameraList::getSaveCount()));
  printf("count: %u\n", static_cast<unsigned>(CameraList::size()));
  for (size_t n = 0; n < CameraList::size(); n++) {
    const auto camera = CameraList::get(n);
    printf("camera%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
    printf("camera%u.type: %lu\n", static_cast<unsigned>(n),
           static_cast<unsigned long>(camera->getType()));
    // The same list carries saved cameras and scan results, and a scan can
    // rediscover a saved camera. Say which each row is.
    const bool saved = CameraList::isSaved(camera.get());
    printf("camera%u.saved: %s\n", static_cast<unsigned>(n), saved ? "true" : "false");
    // 'connect' resolves the saved-catalog index, not this list index. Report
    // the mapping so a script does not connect a different body than the one
    // it just listed.
    const int32_t savedIndex = CameraList::savedIndex(camera.get());
    printf("camera%u.saved_index: %ld\n", static_cast<unsigned>(n), static_cast<long>(savedIndex));
    printf("camera%u.selected: %s\n", static_cast<unsigned>(n),
           camera->isActive() ? "true" : "false");
  }
}

const char *deleteCameras(int32_t index) {
  CameraList::load();
  if ((index >= 0) && (static_cast<size_t>(index) >= CameraList::size())) {
    ESP_LOGE(LOG_TAG, "console: no saved camera at index %ld", index);
    return "no_saved_camera";
  }

  unsigned deleted = 0;
  bool persisted = true;
  const auto snapshot = CameraList::savedSnapshot();
  for (size_t n = 0; n < snapshot.size(); n++) {
    if ((index >= 0) && (n != static_cast<size_t>(index))) {
      continue;
    }
    if (CameraList::remove(snapshot[n].get())) {
      printf("deleted: %s\n", snapshot[n]->getName().c_str());
      deleted++;
    } else {
      persisted = false;
    }
  }
  printf("count: %u\n", deleted);
  CameraList::load();
  return persisted ? "ok" : "persistence_failed";
}

bool connectCamera(int32_t index) {
  CameraList::load();
  if (index >= 0) {
    // An index replaces whatever the multi-connect selection holds.
    for (size_t n = 0; n < CameraList::size(); n++) {
      CameraList::get(n)->setActive(false);
    }
    if (static_cast<size_t>(index) >= CameraList::size()) {
      ESP_LOGE(LOG_TAG, "console: no camera at index %ld", index);
      return false;
    }
    CameraList::get(index)->setActive(true);
  }

  auto &control = Control::getInstance();
  for (size_t n = 0; n < CameraList::size(); n++) {
    auto camera = CameraList::get(n);
    if (camera->isActive()) {
      control.addActive(camera);
    }
  }
  control.connectAll(Settings::load<Settings::RECONNECT>());
  return true;
}

bool connectSavedCamera(uint8_t cameraId) {
  auto &control = Control::getInstance();
  if (Scan::getInstance().isActive() || (control.getState() != Control::STATE_IDLE)
      || (control.getTargetCount() != 0)) {
    ESP_LOGW(LOG_TAG, "companion: connect request is busy");
    return false;
  }

  const auto saved = CameraList::savedSnapshot();
  if (cameraId != CameraListProtocol::INDEX_ID_ALL) {
    const auto found = std::find_if(saved.begin(), saved.end(), [cameraId](const auto &camera) {
      return CameraList::getCameraId(camera.get()) == cameraId;
    });
    if (found == saved.end()) {
      ESP_LOGW(LOG_TAG, "companion: no saved camera id %u", static_cast<unsigned>(cameraId));
      return false;
    }
  }

  CameraList::load();
  if (cameraId != CameraListProtocol::INDEX_ID_ALL) {
    for (size_t n = 0; n < CameraList::size(); n++) {
      const auto camera = CameraList::get(n);
      camera->setActive(CameraList::getCameraId(camera.get()) == cameraId);
    }
  }

  for (size_t n = 0; n < CameraList::size(); n++) {
    auto camera = CameraList::get(n);
    if (camera->isActive()) {
      control.addActive(camera);
    }
  }
  control.connectAll(Settings::load<Settings::RECONNECT>());
  return true;
}

void scanCameras(void) {
  auto &scan = Scan::getInstance();
  CameraList::clear();

  if (Settings::load<Settings::FAUXNY>()) {
    CameraList::addFauxNY();
  }

  scan.setMode(static_cast<Scan::Mode>(Settings::load<Settings::SCAN_MODE>()));
  scan.setTimeout(Settings::load<Settings::SCAN_TIMEOUT>());
  scan.clear();
  scan.start(
      [](void *) { printf("scan camera count: %u\n", static_cast<unsigned>(CameraList::size())); },
      NULL, [](void *) { printf("scan finished\n"); });
}

}  // namespace

void UI::init(void) {
  if (g_HeadlessRequestQueue != nullptr) {
    headless_request_t *queued = nullptr;
    while (xQueueReceive(g_HeadlessRequestQueue, static_cast<void *>(&queued), 0) == pdTRUE) {
      const std::unique_ptr<headless_request_t> owned(queued);
#if defined(FURBLE_CONSOLE)
      if (owned->state != nullptr) {
        owned->state->token = "cancelled";
        xSemaphoreGive(owned->state->done);
        owned->state->release();
      }
#endif
    }
    vQueueDelete(g_HeadlessRequestQueue);
    g_HeadlessRequestQueue = nullptr;
  }
  g_HeadlessRequestQueue =
      xQueueCreate(HEADLESS_REQUEST_QUEUE_LENGTH, sizeof(headless_request_t *));
  if (g_HeadlessRequestQueue == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create headless console request queue.");
    abort();
  }
}

void UI::shutdown(void) {
  if (g_HeadlessRequestQueue == nullptr) {
    return;
  }
  headless_request_t *queued = nullptr;
  while (xQueueReceive(g_HeadlessRequestQueue, static_cast<void *>(&queued), 0) == pdTRUE) {
    const std::unique_ptr<headless_request_t> owned(queued);
#if defined(FURBLE_CONSOLE)
    if (owned->state != nullptr) {
      owned->state->token = "cancelled";
      xSemaphoreGive(owned->state->done);
      owned->state->release();
    }
#endif
  }
  vQueueDelete(g_HeadlessRequestQueue);
  g_HeadlessRequestQueue = nullptr;
}

bool UI::sendRequest(Request request, int32_t arg) {
  if (g_HeadlessRequestQueue == NULL) {
    return false;
  }

  auto item = std::make_unique<headless_request_t>(headless_request_t {request, arg,
#if defined(FURBLE_CONSOLE)
                                                                       nullptr
#endif
  });
  headless_request_t *queued = item.release();  // the queue owns it once sent
  if (xQueueSend(g_HeadlessRequestQueue, static_cast<void *>(&queued), 0) != pdTRUE) {
    item.reset(queued);
    return false;
  }
  return true;
}

#if defined(FURBLE_CONSOLE)
bool UI::sendRequest(Request request, int32_t arg, RequestResult *result) {
  if (g_HeadlessRequestQueue == NULL || result == nullptr || result->state == nullptr) {
    return false;
  }
  result->state->retain();
  auto item =
      std::make_unique<headless_request_t>(headless_request_t {request, arg, result->state});
  headless_request_t *queued = item.release();  // the queue owns it once sent
  if (xQueueSend(g_HeadlessRequestQueue, static_cast<void *>(&queued), 0) != pdTRUE) {
    item.reset(queued);
    result->state->release();
    return false;
  }
  return true;
}
#endif

void UI::serviceRequests(void) {
  headless_request_t *queued = nullptr;

  while (xQueueReceive(g_HeadlessRequestQueue, static_cast<void *>(&queued), 0) == pdTRUE) {
    std::unique_ptr<headless_request_t> owned(queued);
    headless_request_t &item = *owned;
    const char *token = "ok";
    switch (item.request) {
      case Request::CONNECT:
        token = connectCamera(item.arg) ? "ok" : "invalid_target";
        break;

      case Request::CONNECT_SAVED:
        token = connectSavedCamera(static_cast<uint8_t>(item.arg)) ? "ok" : "invalid_target";
        break;

      case Request::DISCONNECT:
        Scan::getInstance().stop();
        Control::getInstance().disconnect();
        break;

      case Request::SCAN:
        if (item.arg) {
          scanCameras();
        } else {
          Scan::getInstance().stop();
        }
        break;

      case Request::CAMERAS:
        printCameras(item.arg != 0);
        break;

      case Request::DELETE:
        token = deleteCameras(item.arg);
        break;

      case Request::GPS_RELOAD:
        GPS::getInstance().reloadSetting();
        break;

      case Request::GPS_POWER:
        M5.Power.setExtOutput(item.arg != 0, m5::ext_PA);
        break;

      case Request::IR_RELOAD:
        // The IR reload only refreshes the UI menu cache, which the headless
        // build does not have. Nothing to do here.
        break;

      case Request::FEEDBACK_RELOAD:
        Feedback::getInstance().reload();
        break;

      case Request::FEEDBACK_TEST:
        Feedback::getInstance().signal(static_cast<Feedback::event_t>(item.arg), true);
        break;

      case Request::PERF:
      case Request::AUDIT:
        printf("error: not supported in headless build\n");
        break;
    }
#if defined(FURBLE_CONSOLE)
    if (item.state != nullptr) {
      item.state->token = token;
      xSemaphoreGive(item.state->done);
      item.state->release();
    }
#endif
    queued = nullptr;
  }
}
}  // namespace Furble
#endif

extern "C" {

static void vUITask(void *param) {
  (void)param;
  using namespace Furble;
#if defined(FURBLE_NO_DISPLAY)
  // The display build ticks GPS::update() from an LVGL timer (GPS::SERVICE_MS).
  // Headless has no LVGL, so drive the same one second service cadence here so
  // geotag fixes still push to the camera.
  constexpr int64_t GPS_SERVICE_US = 1000 * 1000;
  int64_t nextGPSService = esp_timer_get_time();
  bool reported = false;
  while (true) {
    Platform::getInstance().update();
    CrashLog::update();
    MemoryPressure::tick();
    // Keep this loop in step with UI::task(), which owns the GUI request queue.
    UI::serviceRequests();
    Scan::getInstance().processPendingCallbacks();
    const int64_t now = esp_timer_get_time();
    if (now >= nextGPSService) {
      GPS::getInstance().update();
      (void)TimezoneGPS::service();
      nextGPSService = now + GPS_SERVICE_US;
    }
    if (!reported) {
      BootTimeline::mark(BootTimeline::SERVICES_READY);
      BootTimeline::logSummary();
      reported = true;
    }
    // Idle at 50 ms instead of the display task's 5 ms. The request queue is
    // still drained every pass; the sleep only shortens when the 1 Hz GPS
    // service deadline is nearer, so the geotag cadence is unchanged.
    vTaskDelay(pdMS_TO_TICKS(HeadlessPolicy::loopSleepMs(esp_timer_get_time(), nextGPSService)));
  }
#else
  auto interval = Settings::load<Settings::INTERVAL>();
  auto ui = UI(interval);

  ui.task();
#endif
}

void app_main() {
  BaseType_t xRet;
  TaskHandle_t xControlHandle = NULL;

  Furble::BootTimeline::begin();
  // PSRAM boot init and its memory test run before app_main, so this mark has
  // the app_main timestamp. resetToAppMs() carries the cycle-counter value that
  // includes both.
  Furble::BootTimeline::mark(Furble::BootTimeline::PSRAM_READY);

  ESP_LOGI(LOG_TAG, "furble version: '%s'", FURBLE_VERSION);

  // Settings must come up before Platform: Platform reads FB_OUTPUT to
  // decide cfg.internal_spk ahead of M5.begin()
  Furble::Settings::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::SETTINGS_DONE);
  // Print an unclean previous-boot record once, before the services that can
  // fail. The record lives in RTC memory and never needs a flash write.
  Furble::CrashLog::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::CRASH_LOG_DONE);
  Furble::Timezone::apply(
      Furble::Timezone::clamp(Furble::Settings::load<Furble::Settings::TIMEZONE>()));

  // The network stack is independent of the display. Bring it up before the
  // radio so WiFi::init() can start associating under the splash instead of
  // after every other service has initialized.
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  Furble::BootTimeline::mark(Furble::BootTimeline::NETIF_DONE);

  Furble::Platform::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::PLATFORM_DONE);
  Furble::TimeKeeper::init();

  // The display is up now, so the boot splash can cover the rest of init. It
  // reads its own enable, draws through M5GFX, and every hook self-gates. The
  // stage count below must match the number of step() calls before finish().
  Furble::BootScreen::begin(6
#if defined(FURBLE_ETHERNET)
                            + 1
#endif
  );

  Furble::IR::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::IR_DONE);
  Furble::BootScreen::step("Infrared");
  Furble::Feedback::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::FEEDBACK_DONE);
  Furble::BootScreen::step("Feedback");
  Furble::SD::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::SD_DONE);
  Furble::BootScreen::step("Storage");

  // Platform::init() boots at the default frequency, apply the stored one now
  Furble::Platform::getInstance().setCPUMaxFreq(
      Furble::Settings::load<Furble::Settings::CPU_FREQ>());

#if defined(FURBLE_M5STICKS3)
  // The watchdog enable lives in NVS and needs the platform up
  Furble::Platform::getInstance().watchdogEnable(
      Furble::Settings::load<Furble::Settings::WATCHDOG>());
#endif
  Furble::BootTimeline::mark(Furble::BootTimeline::CPU_POLICY_DONE);
  Furble::BootScreen::step("Power");

  Furble::Device::init(Furble::Settings::load<esp_power_level_t>(Furble::Settings::TX_POWER));
  Furble::BootTimeline::mark(Furble::BootTimeline::BLE_DONE);
  Furble::BootScreen::step("Bluetooth");
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
  // Prepare the TLS identity while the main task still has its full internal
  // stack and before WiFi/display allocations fragment the heap. The identity
  // generation writes NVS, which must run on an internal stack anyway.
  Furble::WebUI::initEarly();
  Furble::BootTimeline::mark(Furble::BootTimeline::WEBUI_EARLY_DONE);
#endif
#if !defined(FURBLE_ETHERNET)
  // Start the station after BLE and the WebUI TLS identity, so the identity
  // still sees the full internal heap, but before companion and MQTT. The
  // association now overlaps their init instead of extending the boot after
  // them. Ethernet builds start WiFi after Ethernet::init() so a live wire
  // still defers the station.
  Furble::WiFi::init();
#endif
  Furble::Companion::getInstance().init();
  Furble::BootTimeline::mark(Furble::BootTimeline::COMPANION_DONE);
  Furble::BootScreen::step("Companion");
#if defined(FURBLE_MQTT) && FURBLE_MQTT
  Furble::MQTT::init();
  Furble::BootTimeline::mark(Furble::BootTimeline::MQTT_INIT_DONE);
#endif

#if defined(FURBLE_ETHERNET)
  if (!Furble::Ethernet::init()) {
    ESP_LOGE(LOG_TAG, "Waveshare Ethernet initialization failed.");
  }
  Furble::BootScreen::step("Ethernet");
  // A wired address that is already up must defer the station. Starting WiFi
  // after Ethernet keeps that ordering intact.
  Furble::WiFi::init();
#endif

  auto &control = Furble::Control::getInstance();
  xRet = xTaskCreate(control_task, "control", 8192, &control, 4, &xControlHandle);
  if (xRet != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create control task.");
    abort();
  }
  Furble::BootTimeline::mark(Furble::BootTimeline::CONTROL_TASK_DONE);

#if defined(FURBLE_NO_DISPLAY)
  // The display build starts GPS when the UI constructs. Headless has no UI, so
  // bring GPS up here: this starts the UART task and applies the stored enable
  // state, then vUITask() ticks GPS::update() to push geotag fixes.
  Furble::GPS::init();
#endif

#if defined(FURBLE_NO_DISPLAY)
  Furble::UI::init();
#endif
#if defined(FURBLE_WEBUI) && FURBLE_WEBUI
  Furble::WebUI::init();
#endif

  // Developer only, compiled out unless FURBLE_CONSOLE is defined
  Furble::Console::init();

  // Hold "Ready" briefly, then the UI constructor takes the screen with LVGL.
  Furble::BootScreen::finish();

  // Run the GUI or headless task in the host task (here).
  vUITask(NULL);
}
}
