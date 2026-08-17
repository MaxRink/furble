#include <freertos/FreeRTOS.h>

#include "Device.h"
#include "Scan.h"

#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"

extern "C" {

static void vUITask(void *param) {
  using namespace Furble;
  auto interval = Settings::load<Settings::INTERVAL>();
  auto ui = UI(interval);

  ui.task();
}

void app_main() {
  BaseType_t xRet;
  TaskHandle_t xControlHandle = NULL;

  ESP_LOGI(LOG_TAG, "furble version: '%s'", FURBLE_VERSION);

  Furble::Platform::init();
  Furble::Settings::init();
  Furble::IR::init();

  // Platform::init() runs before NVS exists, apply the stored frequency now
  Furble::Platform::getInstance().setCPUMaxFreq(
      Furble::Settings::load<Furble::Settings::CPU_FREQ>());

#if defined(FURBLE_M5STICKS3)
  // Same ordering constraint, the watchdog enable lives in NVS
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

  // Developer only, compiled out unless FURBLE_CONSOLE is defined
  Furble::Console::init();

  // Run UI in host task (here)
  vUITask(NULL);
}
}
