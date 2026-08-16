#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#if defined(FURBLE_NO_DISPLAY)
#include <M5Unified.h>
#endif

#include "Device.h"
#include "Scan.h"

#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#if defined(FURBLE_NO_DISPLAY)
#include "FurbleGPS.h"
#endif
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"

#if defined(FURBLE_NO_DISPLAY) && defined(FURBLE_CONSOLE)
namespace Furble {
namespace {

constexpr UBaseType_t HEADLESS_REQUEST_QUEUE_LENGTH = 8;

typedef struct {
  UI::Request request;
  int32_t arg;
} headless_request_t;

QueueHandle_t g_HeadlessRequestQueue = NULL;

void printCameras(bool reload) {
  if (reload) {
    CameraList::load();
  }

  printf("saved: %u\n", static_cast<unsigned>(CameraList::getSaveCount()));
  printf("count: %u\n", static_cast<unsigned>(CameraList::size()));
  for (size_t n = 0; n < CameraList::size(); n++) {
    const auto *camera = CameraList::get(n);
    printf("camera%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
    printf("camera%u.type: %lu\n", static_cast<unsigned>(n),
           static_cast<unsigned long>(camera->getType()));
  }
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
    auto *camera = CameraList::get(n);
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

  const headless_request_t item = {request, arg};
  return xQueueSend(g_HeadlessRequestQueue, &item, 0) == pdTRUE;
}

void UI::serviceRequests(void) {
  headless_request_t item;

  while (xQueueReceive(g_HeadlessRequestQueue, &item, 0) == pdTRUE) {
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

      case Request::GPS_RELOAD:
        GPS::getInstance().reloadSetting();
        break;

      case Request::GPS_POWER:
        M5.Power.setExtOutput(item.arg != 0, m5::ext_PA);
        break;
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
  while (true) {
    Platform::getInstance().update();
#if defined(FURBLE_CONSOLE)
    // Keep this loop in step with UI::task(), which owns the GUI request queue.
    UI::serviceRequests();
#endif
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
  Furble::IR::init();
  Furble::Feedback::init();

  // Platform::init() boots at the default frequency, apply the stored one now
  Furble::Platform::getInstance().setCPUMaxFreq(
      Furble::Settings::load<Furble::Settings::CPU_FREQ>());

#if defined(FURBLE_M5STICKS3)
  // The watchdog enable lives in NVS and needs the platform up
  Furble::Platform::getInstance().watchdogEnable(
      Furble::Settings::load<Furble::Settings::WATCHDOG>());
#endif

  Furble::Device::init(Furble::Settings::load<esp_power_level_t>(Furble::Settings::TX_POWER));
  Furble::Companion::getInstance().init();

  auto &control = Furble::Control::getInstance();
  xRet = xTaskCreate(control_task, "control", 8192, &control, 4, &xControlHandle);
  if (xRet != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create control task.");
    abort();
  }

#if defined(FURBLE_NO_DISPLAY) && defined(FURBLE_CONSOLE)
  Furble::UI::init();
#endif

  // Developer only, compiled out unless FURBLE_CONSOLE is defined
  Furble::Console::init();

  // Run the GUI or headless task in the host task (here).
  vUITask(NULL);
}
}
