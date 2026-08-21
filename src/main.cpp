#include <freertos/FreeRTOS.h>

#include "Device.h"
#include "Scan.h"

#include "FurbleBootScreen.h"
#include "FurbleCompanion.h"
#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurbleSD.h"
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

  // Settings must come up before Platform: Platform reads FB_OUTPUT to
  // decide cfg.internal_spk ahead of M5.begin()
  Furble::Settings::init();
  Furble::Platform::init();

  // The display is up now, so the boot splash can cover the rest of init. It
  // reads its own enable, draws through M5GFX, and every hook self-gates. The
  // stage count below must match the number of step() calls before finish().
  Furble::BootScreen::begin(6);

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

  auto &control = Furble::Control::getInstance();
  xRet = xTaskCreate(control_task, "control", 8192, &control, 4, &xControlHandle);
  if (xRet != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create control task.");
    abort();
  }

  // Developer only, compiled out unless FURBLE_CONSOLE is defined
  Furble::Console::init();

  // Hold "Ready" briefly, then the UI constructor takes the screen with LVGL.
  Furble::BootScreen::finish();

  // Run UI in host task (here)
  vUITask(NULL);
}
}
