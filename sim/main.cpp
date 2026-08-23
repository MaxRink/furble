#include <M5GFX.h>

#include <cctype>
#include <cstdlib>
#include <string>

#include <freertos/FreeRTOS.h>

#include "CameraList.h"
#include "Device.h"
#include "FurbleBootScreen.h"
#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "capture.h"
#include "driver.h"

const char *LOG_TAG = FURBLE_STR;

namespace {

int runSimulator(bool *) {
  using namespace Furble;

  Platform::init();
  Sim::startProfiler();
  Sim::preparePreferences();
  Settings::init();
  Sim::applyScenarioSettings();
  Platform::getInstance().setCPUMaxFreq(Settings::load<Settings::CPU_FREQ>());
#if defined(FURBLE_M5STICKS3)
  Platform::getInstance().watchdogEnable(Settings::load<Settings::WATCHDOG>());
#endif

  // Mirror the firmware boot splash so scenarios exercise the same path. A
  // scenario can seed "boot_splash false" to cover the disabled boot too.
  BootScreen::begin(6);
  BootScreen::step("Infrared");
  BootScreen::step("Feedback");
  BootScreen::step("Storage");
  BootScreen::step("Power");

  // The boot splash draws straight to the panel before the LVGL UI exists, so
  // a script cannot reach it. Capturing here, mid progress bar, gives docs a
  // real splash frame. Set FURBLE_SIM_CAPTURE_SPLASH to the output PNG path.
  if (const char *splash = std::getenv("FURBLE_SIM_CAPTURE_SPLASH");
      splash != nullptr && splash[0] != '\0') {
    Sim::captureFrame(splash);
  }

  // The companion service mirrors the rig request so the rig transport can
  // attach. Scenarios drive every other setting through their seed lines.
  Settings::save<bool>(Settings::COMPANION, Sim::rigRequested());

  if (Sim::scenarioSettingIsTrue("autoconnect")) {
    CameraList::addFauxNY();
    auto camera = CameraList::last();
    CameraList::save(camera.get());
    camera->setActive(true);
  } else if (Sim::scenarioSettingIsTrue("saved_camera")) {
    // Seed a saved but inactive camera so the Connect and Delete list pages
    // render entries and their main-menu buttons are enabled. Unlike
    // autoconnect this does not mark the camera active, so no connection is
    // attempted at boot.
    CameraList::addFauxNY();
    auto camera = CameraList::last();
    CameraList::save(camera.get());
  }

  // Let capture scripts pick a theme without navigating the roller. The theme
  // is applied once at UI construction, so seed it before the UI exists.
  if (const char *theme = std::getenv("FURBLE_SIM_THEME"); theme != nullptr && theme[0] != '\0') {
    Settings::save<Settings::THEME>(std::string(theme));
  }

  // Let capture scripts and the UI-collision sweep pick a text size without
  // navigating the roller and restarting. Like the theme, the font is chosen
  // once at UI construction from the TEXT_SIZE setting, so seed it here before
  // the UI exists. Accepts a name (small/normal/large, case insensitive) or the
  // numeric setting value (0/1/2).
  if (const char *size = std::getenv("FURBLE_SIM_TEXTSIZE"); size != nullptr && size[0] != '\0') {
    std::string value(size);
    for (char &c : value) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (value == "small" || value == "0") {
      Settings::save<Settings::TEXT_SIZE>(Settings::TEXT_SIZE_SMALL);
    } else if (value == "large" || value == "2") {
      Settings::save<Settings::TEXT_SIZE>(Settings::TEXT_SIZE_LARGE);
    } else {
      Settings::save<Settings::TEXT_SIZE>(Settings::TEXT_SIZE_NORMAL);
    }
  }

  Device::init(Settings::load<esp_power_level_t>(Settings::TX_POWER));
  BootScreen::step("Bluetooth");
  BootScreen::step("Companion");

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  Sim::startRig();

  BootScreen::finish();

  const auto interval = Settings::load<Settings::INTERVAL>();
  UI ui(interval);
  Sim::setBackTarget(&ui);
  Sim::registerUI(&ui);
  ui.task();
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  Furble::Sim::configure(argc, argv);
  return lgfx::Panel_sdl::main(runSimulator, 128);
}
