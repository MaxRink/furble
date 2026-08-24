#include <cstdlib>

#include <M5GFX.h>
#include <M5Unified.h>
#include <SDL2/SDL.h>

#include <driver/uart.h>

#include <Preferences.h>

#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleWatchdog.h"
#include "Scan.h"
#include "clock.h"
#include "driver.h"
#include "platform_state.h"

namespace Furble {

namespace {
bool g_SuppressNextWatchdogFeed = false;

#if defined(FURBLE_M5STICKS3)
using Watchdog::PM1_FEED_PERIOD_MS;
using Watchdog::PM1_TIMEOUT_S;
#endif

}  // namespace

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    Power::init();
    Power::getInstance().configure(Platform::CPU_MAX_FREQ_DEFAULT_MHZ);
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_spk = false;
    config.internal_mic = false;
    config.pmic_button = true;
    M5.begin(config);

#if defined(FURBLE_M5STICKS3)
    instance.m_M5PM1.begin(nullptr);
    // Seed the retained state with the unsafe value used by older firmware.
    // Two calls model the required wake retry. The production boot path below
    // must clear and verify it, so removing that path makes the scenario fail.
    instance.m_M5PM1.setDownloadLock(true);
    instance.m_M5PM1.setDownloadLock(true);
    if (!instance.unlockDownloadRecovery()) {
      ESP_LOGE("platform", "M5PM1 download recovery remains unavailable");
    }
#endif

    // The SDL panel always attaches a mouse-driven touch device, so
    // M5.Touch.isEnabled() is true regardless of the simulated board. A scenario
    // can seed "no_touch true" to detach it and exercise the physical-button
    // (non-touch) UI branch that boards like the M5StickS3 use, including the
    // floating on-screen button indicators. The FURBLE_SIM_NO_TOUCH environment
    // variable does the same, so a capture harness can select the non-touch
    // layout per board without editing the shared scenario script.
    const char *noTouchEnv = std::getenv("FURBLE_SIM_NO_TOUCH");
    const bool noTouch =
        Sim::scenarioSettingIsTrue("no_touch")
        || (noTouchEnv != nullptr && noTouchEnv[0] != '\0' && noTouchEnv[0] != '0');
    if (noTouch) {
      M5.Touch.begin(nullptr);
    }

    // Map keyboard letters to the same emulated button GPIOs M5Unified's PC
    // build reads (BtnA=39, BtnB=38, BtnC=37, BtnPWR=36), so an interactive
    // session with a visible SDL window can drive the physical buttons furble
    // handles on hardware. The M5GFX SDL panel already maps the arrow keys to
    // these pins; the letters just make the button identity explicit. This path
    // needs the SDL event pump, so it only works interactively: headless
    // scenarios drive the buttons through the `btn` command, which injects on
    // the UI task via UI::simPressButton instead of toggling pins.
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_a, 39);  // BtnA
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_b, 38);  // BtnB
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_c, 37);  // BtnC (Core only)
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_p, 36);  // BtnPWR (Stick only)

    instance.m_Init = true;
  }

  return instance;
}

void Platform::init(void) {
  (void)getInstance();
}

uint32_t Platform::tick(void) {
  return Sim::clockMillis();
}

uint8_t Platform::getPWRClickCount(void) {
  return 0;
}

void Platform::update(void) {
  M5.update();
  furble_sim_uart_update();
  Scan::getInstance().update();
#if defined(FURBLE_M5STICKS3)
  if (!Sim::consumeWatchdogFeedSuppression()) {
    watchdogFeed();
  }
#else
  (void)Sim::consumeWatchdogFeedSuppression();
#endif
}

void Platform::restart(void) {
  // The host simulator has no reset vector; ending the process is the closest
  // equivalent for scripted runs.
  Sim::requestExit(0);
}

bool Platform::powerOff(void) {
  Sim::notePowerOff();
  return true;
}

bool Platform::watchdogEnable(bool enable) {
#if defined(FURBLE_M5STICKS3)
  m_WatchdogEnabled = false;
  m_WatchdogLastFeed = tick();
  const uint8_t timeout = enable ? WDT_TIMEOUT_S : 0;
  if (!m5pm1Access([this, timeout]() { return m_M5PM1.wdtSet(timeout); })) {
    return false;
  }
  m_WatchdogEnabled = enable;
  return true;
#else
  (void)enable;
  return true;
#endif
  return true;
}

void Platform::watchdogFeed(void) {
#if defined(FURBLE_M5STICKS3)
  if (!m_WatchdogEnabled) {
    return;
  }

  const uint32_t now = tick();
  if (now - m_WatchdogLastFeed < PM1_FEED_PERIOD_MS) {
    return;
  }
  m_WatchdogLastFeed = now;
  (void)m5pm1Access([this]() { return m_M5PM1.wdtFeed(); });
#endif
}

void Platform::setDisplayOff(bool) {}

bool Platform::canTimedWake(void) {
  // The S3 build models the M5PM1 timed power-on rail. Other board builds
  // retain the production capability gate and cannot self-wake after power
  // off. The marker is persisted through the same host NVS file as resume
  // state, so a second simulator process is a deterministic virtual boot.
#if defined(FURBLE_M5STICKS3)
  return M5.getBoard() == m5::board_t::board_M5StickS3;
#else
  return false;
#endif
}

bool Platform::powerOffUntil(uint32_t seconds) {
  if (seconds == 0) {
    return powerOff();
  }

  // Exercise the firmware failure branch without ending the process. The
  // production implementation returns after failed timer or shutdown setup,
  // and UI::intervalometer then clears the resume record and continues awake.
  if (Sim::scenarioSettingIsTrue("timed_poweroff_fail")) {
    return false;
  }

  Preferences prefs;
  prefs.begin(FURBLE_STR, false);
  prefs.put<bool>("sim_timed_wake", true);
  prefs.end();

  // A real timed power-off does not return. Exit after the marker and NVS
  // resume state are durable. The follow-up simulator invocation represents
  // the PMIC wake and a fresh app_main/UI construction.
  std::_Exit(0);
}

bool Platform::consumeTimedWake(void) {
  if (!canTimedWake()) {
    return false;
  }

  Preferences prefs;
  prefs.begin(FURBLE_STR, false);
  const bool timedWake = prefs.get<bool>("sim_timed_wake", false);
  if (timedWake) {
    prefs.remove("sim_timed_wake");
  }
  prefs.end();
  return timedWake;
}

void Platform::setCPUMaxFreq(uint8_t mhz) {
  m_CPUMaxFreqMHz = isCPUMaxFreqValid(mhz) ? mhz : CPU_MAX_FREQ_DEFAULT_MHZ;
}

uint8_t Platform::getCPUMaxFreq(void) const {
  return m_CPUMaxFreqMHz;
}

bool Platform::isCPUMaxFreqValid(uint8_t mhz) {
  for (const uint8_t freq : CPU_MAX_FREQ_MHZ) {
    if (freq == mhz) {
      return true;
    }
  }
  return false;
}

Platform::pm_config_t Platform::getPMConfig(void) {
  return {m_CPUMaxFreqMHz, 80, false};
}

void Platform::dumpPMLocks(void) {}

bool Platform::hasTicklessIdle(void) {
  return false;
}

const Platform::battery_caps_t &Platform::getBatteryCaps(void) {
  static const battery_caps_t caps = {true, true, false, true};
  return caps;
}

Platform::battery_t Platform::readBattery(void) {
  const auto reading = Sim::batteryReading();
  return {reading.level, reading.voltage, reading.current, reading.charging};
}

Platform::battery_sample_t Platform::sampleBattery(void) {
  // Keep the same sample boundary as firmware while allowing a scenario to
  // replace the deterministic reading between 5-second timer ticks.
  const battery_t battery = readBattery();
  m_BatterySample.battery = battery;
  m_BatterySample.meanLevel = battery.level;
  m_BatterySample.meanVoltage = battery.voltage;
  m_BatterySample.meanCurrent = battery.current;
  m_BatterySample.displayLevel = battery.level;
  return m_BatterySample;
}

uint16_t Platform::getBatteryCapacity(void) {
  return 250;
}

uint32_t Platform::getBatteryFailCount(void) {
  return 0;
}

namespace Sim {

void suppressNextWatchdogFeed(void) {
  g_SuppressNextWatchdogFeed = true;
}

bool consumeWatchdogFeedSuppression(void) {
  const bool suppressed = g_SuppressNextWatchdogFeed;
  g_SuppressNextWatchdogFeed = false;
  return suppressed;
}

const char *watchdogState(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  if (platform.m_M5PM1.watchdogExpired()) {
    return "expired";
  }
  return platform.m_WatchdogEnabled ? "armed" : "disabled";
#else
  return "unavailable";
#endif
}

const char *downloadLockState(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  return platform.downloadRecoveryUnlocked() ? "unlocked" : "locked";
#else
  return "unavailable";
#endif
}

}  // namespace Sim

}  // namespace Furble
