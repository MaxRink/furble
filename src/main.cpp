#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#if defined(FURBLE_NO_DISPLAY)
#include <atomic>

#include <M5Unified.h>
#include <esp_timer.h>
#endif

#include "Device.h"
#include "Scan.h"

#include "FurbleBatteryStatus.h"
#include "FurbleBootScreen.h"
#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#if defined(FURBLE_ETHERNET)
#include "FurbleEthernet.h"
#endif
#if defined(FURBLE_NO_DISPLAY)
#include "FurbleGPS.h"
#endif
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleUI.h"

#if defined(FURBLE_NO_DISPLAY)
namespace Furble {

std::mutex g_IMUMutex;

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

#if defined(FURBLE_NO_DISPLAY) && defined(FURBLE_CONSOLE)
namespace Furble {
namespace {

constexpr UBaseType_t HEADLESS_REQUEST_QUEUE_LENGTH = 8;

typedef struct {
  UI::Request request;
  int32_t arg;
} headless_request_t;

QueueHandle_t g_HeadlessRequestQueue = NULL;

/**
 * Outcome token for the request just serviced.
 *
 * The same contract the display build answers: a workflow handler prints its
 * own lines and serviceRequests() ends the answer with 'result: <token>', so
 * a host script reads one machine readable outcome whichever build it drives.
 */
std::atomic<const char *> g_ConsoleResult {nullptr};

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
    printf("camera%u.saved: %s\n", static_cast<unsigned>(n),
           CameraList::isSavedAddress(camera.get()) ? "true" : "false");
    printf("camera%u.selected: %s\n", static_cast<unsigned>(n),
           camera->isActive() ? "true" : "false");
  }
}

void deleteCameras(int32_t index) {
  // The same answer the display build's Request::DELETE handler prints, minus
  // the Delete page refresh this build has no page to do. A refusal is a
  // console line rather than a log line: it is the answer to a command a
  // script just typed, so it has to reach the console stream whatever the log
  // level is.
  CameraList::load();
  if ((index >= 0) && (static_cast<size_t>(index) >= CameraList::size())) {
    g_ConsoleResult = "no_saved_camera";
    printf("error: no saved camera at index %ld\n", static_cast<long>(index));
    return;
  }

  unsigned deleted = 0;
  for (size_t n = 0; n < CameraList::size(); n++) {
    if ((index >= 0) && (n != static_cast<size_t>(index))) {
      continue;
    }
    printf("deleted: %s\n", CameraList::get(n)->getName().c_str());
    CameraList::remove(CameraList::get(n).get());
    deleted++;
  }
  g_ConsoleResult = "ok";
  printf("count: %u\n", deleted);
  CameraList::load();
}

void connectCamera(int32_t index) {
  CameraList::load();
  if (index >= 0) {
    // An index replaces whatever the multi-connect selection holds.
    for (size_t n = 0; n < CameraList::size(); n++) {
      CameraList::get(n)->setActive(false);
    }
    if (static_cast<size_t>(index) >= CameraList::size()) {
      ESP_LOGE(LOG_TAG, "console: no camera at index %ld", index);
      return;
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
  g_HeadlessRequestQueue = xQueueCreate(HEADLESS_REQUEST_QUEUE_LENGTH, sizeof(headless_request_t));
  if (g_HeadlessRequestQueue == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create headless console request queue.");
    abort();
  }
}

bool UI::sendRequest(Request request, int32_t arg) {
  if (g_HeadlessRequestQueue == NULL) {
    return false;
  }

  // Drop the previous answer here, so a console verb which waits cannot read a
  // stale token as this request's outcome.
  g_ConsoleResult = nullptr;

  const headless_request_t item = {request, arg};
  return xQueueSend(g_HeadlessRequestQueue, &item, 0) == pdTRUE;
}

const char *UI::consoleResult(void) {
  return g_ConsoleResult;
}

void UI::serviceRequests(void) {
  headless_request_t item;

  while (xQueueReceive(g_HeadlessRequestQueue, &item, 0) == pdTRUE) {
    // The workflow verbs end with one machine readable outcome line. The
    // requests which predate it leave this null and print nothing extra.
    g_ConsoleResult = nullptr;

    switch (item.request) {
      case Request::CONNECT:
        connectCamera(item.arg);
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
        deleteCameras(item.arg);
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
    }

    const char *result = g_ConsoleResult;
    if (result != nullptr) {
      printf("result: %s\n", result);
    }
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
  while (true) {
    Platform::getInstance().update();
#if defined(FURBLE_CONSOLE)
    // Keep this loop in step with UI::task(), which owns the GUI request queue.
    UI::serviceRequests();
#endif
    Scan::getInstance().processPendingCallbacks();
    const int64_t now = esp_timer_get_time();
    if (now >= nextGPSService) {
      GPS::getInstance().update();
      nextGPSService = now + GPS_SERVICE_US;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
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

  ESP_LOGI(LOG_TAG, "furble version: '%s'", FURBLE_VERSION);

  // Settings must come up before Platform: Platform reads FB_OUTPUT to
  // decide cfg.internal_spk ahead of M5.begin()
  Furble::Settings::init();
  Furble::Platform::init();
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
  Furble::BootScreen::step("Infrared");
  Furble::Feedback::init();
  Furble::BootScreen::step("Feedback");
  Furble::SD::init();
  Furble::BootScreen::step("Storage");

  // Platform::init() boots at the default frequency, apply the stored one now
  Furble::Platform::getInstance().setCPUMaxFreq(
      Furble::Settings::load<Furble::Settings::CPU_FREQ>());

#if defined(FURBLE_M5STICKS3)
  // The watchdog enable lives in NVS and needs the platform up
  Furble::Platform::getInstance().watchdogEnable(
      Furble::Settings::load<Furble::Settings::WATCHDOG>());
#endif
  Furble::BootScreen::step("Power");

  Furble::Device::init(Furble::Settings::load<esp_power_level_t>(Furble::Settings::TX_POWER));
  Furble::BootScreen::step("Bluetooth");
  Furble::Companion::getInstance().init();
  Furble::BootScreen::step("Companion");

#if defined(FURBLE_ETHERNET)
  if (!Furble::Ethernet::init()) {
    ESP_LOGE(LOG_TAG, "Waveshare Ethernet initialization failed.");
  }
  Furble::BootScreen::step("Ethernet");
#endif

  auto &control = Furble::Control::getInstance();
  xRet = xTaskCreate(control_task, "control", 8192, &control, 4, &xControlHandle);
  if (xRet != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create control task.");
    abort();
  }

#if defined(FURBLE_NO_DISPLAY)
  // The display build starts GPS when the UI constructs. Headless has no UI, so
  // bring GPS up here: this starts the UART task and applies the stored enable
  // state, then vUITask() ticks GPS::update() to push geotag fixes.
  Furble::GPS::init();
#endif

#if defined(FURBLE_NO_DISPLAY) && defined(FURBLE_CONSOLE)
  Furble::UI::init();
#endif

  // Developer only, compiled out unless FURBLE_CONSOLE is defined
  Furble::Console::init();

  // Hold "Ready" briefly, then the UI constructor takes the screen with LVGL.
  Furble::BootScreen::finish();

  // Run the GUI or headless task in the host task (here).
  vUITask(NULL);
}
}
