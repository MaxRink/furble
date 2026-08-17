#include <M5GFX.h>

#include <freertos/FreeRTOS.h>

#include "Device.h"
#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "driver.h"

const char *LOG_TAG = FURBLE_STR;

namespace {

int runSimulator(bool *) {
  using namespace Furble;

  Platform::init();
  Settings::init();

  // Keep the host run useful without requiring an NVS seed step.
  Settings::save<bool>(Settings::GPS, true);
  Settings::save<bool>(Settings::FAUXNY, true);
  Settings::save<bool>(Settings::MULTICONNECT, false);
  Settings::save<bool>(Settings::RECONNECT, false);
  Settings::save<bool>(Settings::AUTOCONNECT, false);

  Device::init(Settings::load<esp_power_level_t>(Settings::TX_POWER));

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  const auto interval = Settings::load<Settings::INTERVAL>();
  UI ui(interval);
  ui.task();
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  Furble::Sim::configure(argc, argv);
  return lgfx::Panel_sdl::main(runSimulator, 128);
}
