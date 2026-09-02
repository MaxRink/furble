#include <M5GFX.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <freertos/FreeRTOS.h>

#include "CameraList.h"
#include "Device.h"
#include "FurbleBootScreen.h"
#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"
#include "Scan.h"
#include "ble_sim.h"
#include "capture.h"
#include "driver.h"
#include "watchdog.h"

namespace {

std::atomic<bool> panelReady {false};

int runSimulator() {
  using namespace Furble;

  Sim::watchdogRegisterThread("simulator");
  Sim::watchdogPhase("panel bring-up");
  Platform::init();
  // Panel_sdl::main starts its render loop concurrently with this callback.
  // M5GFX registers the panel from Platform::init without protecting its
  // global monitor list, so let our main thread observe that registration
  // before it asks Panel_sdl to traverse the list.
  panelReady.store(true, std::memory_order_release);
  // Boot runs before any simulator task exists and does not advance virtual
  // time, so the phase is the only progress the stall watchdog can see across
  // it. Record each step: a slow but progressing boot on a loaded host keeps
  // resetting the watchdog, and a wedged one names the step it stopped at.
  Sim::watchdogPhase("preferences");
  Sim::startProfiler();
  Sim::preparePreferences();
  Sim::watchdogPhase("settings");
  Settings::init();
  Sim::watchdogPhase("scenario settings");
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

  // Bring the virtual radio up right after the BLE stack, the same order the
  // firmware brings up NimBLE before any camera exists. The peers advertise to
  // the production Scan and answer the production Camera connect paths.
  std::string topology = Sim::scenarioSetting("ble_peers", "none");
  const bool connectFail = Sim::scenarioSettingIsTrue("connect_fail");
  if (connectFail && topology == "none") {
    // A camera that never establishes a link needs a radio to fail at. FauxNY
    // has none, so the connect-failure seed implies one real virtual peer.
    topology = "fuji";
  }
  Sim::watchdogPhase("virtual radio");
  Sim::bleStartPeers(topology);
  if (topology != "none" && (connectFail || Sim::scenarioSettingIsTrue("ble_saved"))) {
    Sim::bleSaveRegisteredPeers();
  }
  if (connectFail) {
    Sim::bleSetConnectFail(true);
  }

  BootScreen::step("Bluetooth");
  BootScreen::step("Companion");

  Sim::watchdogPhase("control task");
  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  Sim::startRig();

  BootScreen::finish();

  const auto interval = Settings::load<Settings::INTERVAL>();
  UI ui(interval);
  Sim::setBackTarget(&ui);
  Sim::registerUI(&ui);
  Sim::watchdogPhase("running");
  ui.task();
  Sim::watchdogPhase("teardown");

  // Tear the control session down before anything else unwinds. The firmware
  // never leaves UI::task, so on device this teardown only runs on the restart
  // path; on the host, process exit would otherwise destroy the NimBLE client
  // pool and the control targets in an unspecified static order, and the
  // target destructor's Camera::disconnect() would dereference a freed client.
  //
  // A false return means the teardown force-completed on its timeout instead of
  // settling: a target task was still inside Camera::disconnect(), or a connect
  // was still in flight. On device that is survivable because esp_restart()
  // follows immediately, but in a simulator run it is a defect that used to be
  // reported only as a log line nothing read, so every seed passed while every
  // seed force-completed. Fail the run.
  if (!control.disconnect(Control::DISCONNECT_WAIT_MAX_MS, /*forRestart=*/true)) {
    std::fprintf(stderr, "Camera teardown force-completed on the %lu ms disconnect timeout.\n",
                 static_cast<unsigned long>(Control::DISCONNECT_WAIT_MAX_MS));
    Sim::requestFailureExit();
  }
  Scan::getInstance().stop();
  Scan::getInstance().joinStartProbes();
  // No rig worker may arm or touch a service timer after this point. The
  // esp_timer API deletes callbacks asynchronously on hardware, so keep the
  // callback argument alive until the simulator dispatcher has joined.
  Sim::quiesceRig();
  furble_sim_stop_all_tasks();
  // The virtual peers are released only after every task has joined. The
  // control task, its per-target tasks and the virtual radio all hold pointers
  // into a peer, so freeing them while any of those still runs is a
  // use-after-free at shutdown.
  Sim::bleStopPeers();
  Sim::stopRig();
  Sim::watchdogUnregisterThread();
  return Sim::exitResult();
}

}  // namespace

int main(int argc, char **argv) {
  Furble::Sim::configure(argc, argv);
  Furble::Sim::watchdogRegisterThread("main");
  Furble::Sim::watchdogStart();
  if (lgfx::Panel_sdl::setup() != 0) {
    return 1;
  }

  int simulatorResult = 0;
  std::thread simulator([&simulatorResult]() { simulatorResult = runSimulator(); });
  // Sleep rather than spin. This wait is unbounded on purpose: a panel that
  // never comes up is a defect, not a slow host, and the stall watchdog turns
  // it into a thread dump and a non zero exit within its host bound. A yield
  // loop instead burned a whole core while it waited, which made the very load
  // that provokes such a stall worse.
  while (!panelReady.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  while (!Furble::Sim::exitRequested() && lgfx::Panel_sdl::loop() == 0) {
  }

  if (!Furble::Sim::exitRequested()) {
    Furble::Sim::requestExit(0);
  }
  simulator.join();
  const int closeResult = lgfx::Panel_sdl::close();

  // A scenario `restart` step reboots the simulated device by re-executing this
  // binary. The re-exec waits until here so the reboot follows the same orderly
  // shutdown a clean exit takes: every task stopped and joined, the panel
  // closed. Any failure raised between the step and this point wins over the
  // reboot, so a broken scenario reports its result instead of booting again.
  // The enforced liveness invariant is one of those failures (it calls
  // requestExit(1)); a scenario that seeds `liveness_check false` has opted out
  // of failing, so its recorded violations deliberately do not cancel a reboot.
  if (Furble::Sim::restartRequested() && simulatorResult == 0 && closeResult == 0) {
    Furble::Sim::restartProcess();
  }

  Furble::Sim::watchdogStop();
  return simulatorResult == 0 ? closeResult : simulatorResult;
}
