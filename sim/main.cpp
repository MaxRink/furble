#include <M5GFX.h>

#include <freertos/FreeRTOS.h>

#include "CameraList.h"
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
  Sim::startProfiler();
  Sim::preparePreferences();
  Settings::init();
  Sim::applyScenarioSettings();
  Platform::getInstance().setCPUMaxFreq(Settings::load<Settings::CPU_FREQ>());

  // The companion service mirrors the rig request so the rig transport can
  // attach. Scenarios drive every other setting through their seed lines.
  Settings::save<bool>(Settings::COMPANION, Sim::rigRequested());

  if (Sim::scenarioSettingIsTrue("autoconnect")) {
    CameraList::addFauxNY();
    auto *camera = CameraList::last();
    CameraList::save(camera);
    camera->setActive(true);
  }

  Device::init(Settings::load<esp_power_level_t>(Settings::TX_POWER));

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  Sim::startRig();

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
