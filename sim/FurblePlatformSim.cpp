#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <string>

#include <M5GFX.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <driver/uart.h>

#include "FurbleBatteryStatus.h"
#include "FurbleBoard.h"
#include "FurbleBoardCaps.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurblePlatformPolicy.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "FurbleWatchdog.h"
#include "Scan.h"
#include "clock.h"
#include "driver.h"
#include "hardware_model.h"
#include "platform_state.h"
#include "ui_buttons.h"

namespace Furble {

namespace {
bool g_SuppressNextWatchdogFeed = false;
Sim::boot_settings_t g_BootSettings = {false, 0};
// The profile row a scenario selects, board_unknown to use the detected board.
m5::board_t g_BoardProfile = m5::board_t::board_unknown;

#if defined(FURBLE_M5STICKS3)
using Watchdog::PM1_FEED_PERIOD_MS;
using Watchdog::PM1_TIMEOUT_S;
#endif

}  // namespace

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    // Record the same settings consumed at the firmware Platform boundary.
    // The SDL platform below deliberately does not turn these into physical
    // M5 IMU or speaker configuration.
    const Sim::boot_settings_t bootSettingsSnapshot = {
        Settings::load<bool>(Settings::IMU),
        Settings::load<uint8_t>(Settings::FB_OUTPUT),
    };
    Sim::captureBootSettings(bootSettingsSnapshot);
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
    // A PM1 watchdog expiry resets the system, not a status bit. The host has no
    // reset line, so the model records the power cycle in the hardware model and
    // the run continues under the new reset reason.
    M5PM1::setPowerCycleHook([]() { Sim::hwNoteWatchdogCycle(); });
    // Seed the retained state with the unsafe value used by older firmware.
    // Two calls model the required wake retry. The production boot path below
    // must clear and verify it, so removing that path makes the scenario fail.
    instance.m_M5PM1.setDownloadLock(true);
    instance.m_M5PM1.setDownloadLock(true);
    if (!instance.unlockDownloadRecovery()) {
      ESP_LOGE("platform", "M5PM1 download recovery remains unavailable");
    }
#endif

    // The panel class is not the board. The SDL panel always attaches a
    // mouse-driven touch device, so an unseeded run would render the touch
    // layout on a board that has no touch panel. Default to the modelled
    // capability, which is the same answer M5.Touch.isEnabled() gives on the
    // device, and let a scenario that measures the touch layout opt in with
    // "seed touch true". "seed no_touch true" and FURBLE_SIM_NO_TOUCH do the
    // reverse and force the physical-button layout; FURBLE_SIM_NO_TOUCH=0
    // restores touch for a deliberate mixed-input session.
    const std::string touchSeed = Sim::scenarioSetting("touch", "");
    const bool touchCapability =
        touchSeed.empty() ? Sim::hwTouch() : Sim::scenarioSettingIsTrue("touch");
    const char *noTouchEnv = std::getenv("FURBLE_SIM_NO_TOUCH");
    const bool forceTouch = noTouchEnv != nullptr && noTouchEnv[0] == '0';
    const bool noTouchSeed = Sim::scenarioSettingIsTrue("no_touch")
                             || (noTouchEnv != nullptr && noTouchEnv[0] != '\0' && !forceTouch);
    const bool noTouch = !forceTouch && (noTouchSeed || !touchCapability);
    // The on-screen button window is a physical-input surface. Keep the panel
    // in the physical-button layout too, otherwise the touch driver interprets
    // a click in the button window as a second touch on the panel and the
    // floating legends disappear.
    if (noTouch || (Sim::uiButtonsRequested() && !forceTouch)) {
      M5.Touch.begin(nullptr);
    }

    // Map keyboard letters to the same emulated button GPIOs M5Unified's PC
    // build reads (BtnA=39, BtnB=38, BtnC=37, BtnPWR=36), so an interactive
    // session with a visible SDL window can drive the physical buttons furble
    // handles on hardware. The M5GFX SDL panel already maps the arrow keys to
    // these pins; the letters just make the button identity explicit. This path
    // needs the SDL event pump and shares the emulated GPIOs with the
    // interactive on-screen control window. Headless scenarios drive the
    // buttons through the `btn` command, which injects on the UI task via
    // UI::simPressButton instead of toggling pins.
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
#if defined(FURBLE_M5STICKS3)
  // The PM1 counts down on its own. This is the tick that lets the model act on
  // a deadline that a scripted stall skipped.
  m_M5PM1.pollDeadlines();
#endif
  // The IR fake holds the EXT_5V rail for a modelled burst window and releases
  // it here once the window has elapsed, the way the production IR task
  // releases its hold after transmit() returns.
  IR::getInstance().poll();
  // Advance the modelled board power path (the IP5306 light-load timer).
  Sim::hwTick();
  // The production Scan expires its own deadline from isActive(); the virtual
  // radio delivers advertisements and the scan-end event on its own task, so
  // there is nothing for the platform tick to pump here any more.
#if defined(FURBLE_M5STICKS3)
  if (!Sim::consumeWatchdogFeedSuppression()) {
    watchdogFeed();
  }
#else
  (void)Sim::consumeWatchdogFeedSuppression();
#endif
}

void Platform::restart(void) {
  // Route every simulated reboot through the same orderly shutdown as the
  // scripted restart verb. The main thread re-execs after all tasks join.
  if (std::getenv("FURBLE_SIM_FUZZ_DIAGNOSTICS") != nullptr) {
    std::fprintf(stderr, "SIM Platform::restart requestRestart()\n");
  }
  Sim::requestRestart();
}

bool Platform::powerOff(void) {
  // Mirror the production refusal so a board row without a power-off path reads
  // the same here as on hardware. `platform.power_off` then stays "no" and a
  // scenario can assert that the request was refused rather than accepted.
  if (PlatformPolicy::powerOffAction(Board::profile().powerOffPath)
      == PlatformPolicy::PowerOffAction::REFUSE) {
    ESP_LOGW("platform", "Power off refused, the %s profile has no power-off path",
             Board::profile().name);
    return false;
  }
  // Record the request before the model answers, so a refused outcome still
  // shows that the whole shutdown sequence ran.
  Sim::notePowerOff();
  return Sim::hwPowerOff();
}

#if defined(FURBLE_M5STICKS3)
bool Platform::unlockDownloadRecovery(void) {
  if (!m5pm1Access([this]() { return m_M5PM1.setDownloadLock(false); })) {
    return false;
  }

  bool locked = true;
  if (!m5pm1Access([this, &locked]() { return m_M5PM1.getDownloadLock(&locked); })) {
    return false;
  }
  Sim::hwSetDownloadLocked(locked);
  return !locked;
}

bool Platform::prepareFlash(void) {
  if (!watchdogEnable(false)) {
    (void)watchdogEnable(true);
    return false;
  }
  uint8_t watchdogCount = 1;
  if (!m5pm1Access([this, &watchdogCount]() { return m_M5PM1.wdtGetCount(&watchdogCount); })
      || watchdogCount != 0) {
    (void)watchdogEnable(true);
    return false;
  }

  if (!unlockDownloadRecovery()) {
    (void)watchdogEnable(true);
    return false;
  }
  return true;
}

bool Platform::downloadRecoveryUnlocked(void) {
  bool locked = true;
  if (!m5pm1Access([this, &locked]() { return m_M5PM1.getDownloadLock(&locked); })) {
    return false;
  }
  return !locked;
}

bool Platform::cancelFlashPreparation(void) {
  return watchdogEnable(true);
}
#endif

bool Platform::watchdogEnable(bool enable) {
#if defined(FURBLE_M5STICKS3)
  m_WatchdogEnabled = false;
  m_WatchdogLastFeed = tick();
  const uint8_t timeout = enable ? PM1_TIMEOUT_S : 0;
  if (!m5pm1Access([this, timeout]() { return m_M5PM1.wdtSet(timeout); })) {
    return false;
  }
  uint8_t count = enable ? 0 : 1;
  if (!m5pm1Access([this, &count]() { return m_M5PM1.wdtGetCount(&count); })
      || (enable ? count == 0 : count != 0)) {
    return false;
  }
  m_WatchdogEnabled = enable;
  Sim::hwSetWatchdogEnabled(enable);
  return true;
#else
  (void)enable;
  return true;
#endif
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
  Sim::hwNoteWatchdogFeed();
#endif
}

void Platform::setDisplayOff(bool off) {
  // The firmware body saves the PM1 status LED level out of PWR_CFG and clears
  // it while the panel is off, then restores it on wake. The EXT 5 V output bit
  // shares that byte, so this path drives the same register instead of a local
  // flag: a write built from a read that a rail change has since invalidated
  // loses the change, and the model counts it. `platform.display_off` reads the
  // same modelled byte.
  Sim::hwSetDisplayOff(off);
  // The IP5306 light-load timer runs on the panel state, so the same call feeds
  // the modelled cutoff.
  Sim::hwSetPanelOff(off);
}

// The IMU interrupt reaches an ESP32 pin only on the boards whose schematic
// routes it there: GPIO35 on the StickC family, and M5PM1 GPIO4 chained to
// GPIO13 on the StickS3. Model the same board answer here so a scenario can
// tell a wake-capable board from one that has to poll.
bool Platform::armMotionWake(void) {
#if defined(FURBLE_M5COREX)
  return false;
#else
  m_MotionWakeArmed = true;
  return true;
#endif
}

void Platform::disarmMotionWake(void) {
  m_MotionWakeArmed = false;
}

bool Platform::motionWakeAsserted(void) const {
  // The host has no interrupt line. The virtual engines report motion through
  // the injected accelerometer instead, so this stays idle.
  return false;
}

bool Platform::motionWakeSample(void) {
  // The host has no interrupt line, so nothing ever asserts.
  return false;
}

uint32_t Platform::motionWakeEdges(void) const {
  return 0;
}

uint32_t Platform::getM5PM1RetryCount(void) const {
  return 0;
}

bool Platform::canTimedWake(void) {
  // The S3 build models the M5PM1 timed power-on rail. Other board builds
  // retain the production capability gate and cannot self-wake after power
  // off. The marker is persisted through the same host NVS file as resume
  // state, so a second simulator process is a deterministic virtual boot.
#if defined(FURBLE_M5STICKS3)
  if (M5.getBoard() == m5::board_t::board_M5StickS3) {
    return true;
  }
#endif
  // A modelled RTC-alarm board, the StickC Plus2 row, wakes through its
  // BM8563 alarm. This is the production gate: the profile's timed wake plus a
  // chip that can be programmed. Without a modelled chip nothing can wake the
  // device, exactly as on hardware.
  return Sim::hwRtcTimedAlarmBoard() && Sim::hwRtcPresent();
}

bool Platform::powerOffUntil(uint32_t seconds) {
  if (seconds == 0) {
    return powerOff();
  }

  // Exercise the firmware failure branch without ending the process. This
  // models a failed timer or wake-marker persistence operation; the UI must
  // clear the resume record and continue the run while awake.
  if (Sim::scenarioSettingIsTrue("timed_poweroff_fail")
      || Sim::scenarioSettingIsTrue("timed_wake_write_fail")) {
    return false;
  }

  Preferences prefs;
  prefs.begin(FURBLE_STR, false);
  const size_t markerWritten = prefs.put<bool>("sim_timed_wake", true);
  prefs.end();

  // Verify both persisted records before modelling the process exit. This
  // sidecar is host-only evidence that the fresh-process test crossed the
  // same persistence barrier as a physical reboot.
  const char *evidencePath = std::getenv("FURBLE_SIM_DEEP_SLEEP_EVIDENCE");
  if (markerWritten == sizeof(bool) && evidencePath != nullptr && evidencePath[0] != '\0') {
    Preferences verify;
    verify.begin(FURBLE_STR, true);
    const bool marker = verify.get<bool>("sim_timed_wake", false);
    const bool resume = verify.isKey("ivl_resume");
    verify.end();
    if (marker && resume) {
      std::ofstream evidence(evidencePath, std::ios::trunc);
      evidence << "resume_and_timed_wake_persisted\n";
    }
  }

  // A real timed power-off does not return. Exit after the marker and NVS
  // resume state are durable. The follow-up simulator invocation represents
  // the PMIC wake and a fresh app_main/UI construction.
  if (markerWritten != sizeof(bool)) {
    return false;
  }
  // The RTC chip counts on its own supply while the SoC is off. Record the
  // sleep interval so the next process boots from an advanced calendar.
  Sim::hwRtcPowerOff(seconds);
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

bool Platform::canCutGpsRail(void) {
  // Firmware reads the board profile. The 320x240 build models the Core
  // Basic, whose profile has no GPS rail. A scenario can also fix the rail on
  // (`seed gps_rail_fixed`) to model a Core or Waveshare board on a Stick.
  return Board::profile().gpsRail && !Sim::hwRailFixed();
}

bool Platform::canTurnDisplayOff(void) {
  // The model owns the IP5306 keep-on bit, so this answers from it: an IP5306
  // board whose bit is clear would drop a panel-off load under the 45 mA
  // light-load floor, which is the firmware's own reason to stay dim. Every
  // other modelled part answers yes.
  return Sim::hwCanTurnDisplayOff();
}

bool Platform::hasGpsBackupSupply(void) {
  // The modelled board carries a backup supply exactly when the virtual
  // receiver has one, `seed gps_m10_backup`.
  return furble_sim_uart_gps_backup();
}

bool Platform::hasTicklessIdle(void) {
  return false;
}

Platform::battery_caps_t Platform::getBatteryCaps(void) {
  // Return a value, not a reference to a function-static record. Console and
  // UI readers run on different host threads and each call refreshes the flags
  // from the hardware model.
  return {Sim::hwGaugeLevel(), Sim::hwGaugeVoltage(), Sim::hwGaugeCurrent(), Sim::hwGaugeCharge()};
}

Platform::battery_t Platform::readBattery(void) {
  if (!Sim::hwBatteryPresent()) {
    return {0, 0, 0, false};
  }
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

Platform::battery_status_t Platform::getBatteryStatus(void) {
  // Same body as firmware: the M5Unified readings behind the board's
  // battery capabilities, which the hardware model supplies here.
  battery_status_t raw = {};
  raw.level = M5.Power.getBatteryLevel();
  raw.voltage = M5.Power.getBatteryVoltage();
  raw.current = M5.Power.getBatteryCurrent();
  raw.charging = static_cast<int>(M5.Power.isCharging()) == 1;
  battery_status_t status = BatteryStatus::gate(getBatteryCaps(), raw);
#if FURBLE_HAS_VBUS_SENSE
  status.vbus = BatteryStatus::vbus(true, M5.Power.getVBUSVoltage());
#else
  status.vbus = BatteryStatus::vbus(false, 0);
#endif
  return status;
}

uint16_t Platform::getBatteryCapacity(void) {
  // The firmware reads Board::profile().batteryCapacityMah at the platform
  // boundary; the simulator answers from the same row, so a board whose
  // capacity is unknown reports 0 rather than the StickS3 figure.
  return Board::profile().batteryCapacityMah;
}

namespace {

/** The model holder behind one production holder, for the shared rail. */
Sim::rail_holder_t simRailHolder(RailHolder holder) {
  return holder == RailHolder::IR ? Sim::rail_holder_t::IR : Sim::rail_holder_t::GPS;
}

/**
 * The production output-mode interlock over the modelled PWR_SRC answer.
 *
 * `seed rail_supply` selects what the PMIC reports (`battery`, `usb`, `port`,
 * `unknown` or `unreadable`), and the decision itself is the production
 * `PlatformPolicy::railEnableVerdict()`.
 */
bool railEnablePermitted(PlatformPolicy::RailEnableVerdict *verdict) {
  const std::string name = Sim::hwRailSupplyName();
  const bool known = name != "unreadable";
  PlatformPolicy::RailSupply supply = PlatformPolicy::RailSupply::UNKNOWN;
  if (name == "usb") {
    supply = PlatformPolicy::RailSupply::USB_5V;
  } else if (name == "port") {
    supply = PlatformPolicy::RailSupply::PORT_5V;
  } else if (name == "battery") {
    supply = PlatformPolicy::RailSupply::BATTERY;
  }
  *verdict = PlatformPolicy::railEnableVerdict(known, supply);
  return *verdict == PlatformPolicy::RailEnableVerdict::ALLOW;
}

}  // namespace

bool Platform::railSwitchable(Rail rail) {
  return (rail == Rail::EXT_5V) && Board::profile().gpsRail;
}

bool Platform::setRail(Rail rail, bool on) {
  if (!railSwitchable(rail)) {
    return false;
  }
  // Same calls as firmware, so the hardware model sees the same rail traffic.
  // The board has one switched 5 V output behind more than one holder, so the
  // hardware model owns it. The model returns the read-back: it refuses a cut
  // while another holder still needs the output, and it keeps the rail on where
  // the board cannot switch it (`seed gps_rail_fixed`) or the PMIC refuses a cut
  // (`seed gps_rail_refused`). The fake receiver follows that read-back so the
  // GPS driver's restart decision sees the same answer as hardware.
  M5.Power.setExtOutput(on, m5::ext_PA);
  if (M5.Power.getExtOutput() != on) {
    M5.Power.setExtOutput(on, m5::ext_PA);
  }
  const bool readBack = Sim::hwSetRail(Sim::rail_holder_t::GPS, on);
  furble_sim_uart_set_rail(readBack);
  return readBack == on;
}

bool Platform::railAcquire(Rail rail, RailHolder holder) {
  if (!railSwitchable(rail)) {
    return false;
  }

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  const bool added = !m_RailAccount.holds(holder);
  const bool switching = !m_RailOn || added;
  if (switching && !railEnablePermitted(&m_RailVerdict)) {
    // The production interlock refuses the enable before the register write,
    // and a refusal leaves the port in the vendor default of input mode. Tell
    // the model the holder is not on the rail, so no holder is left behind.
    m_RailInterlockRefusals++;
    (void)Sim::hwSetRail(simRailHolder(holder), false);
    return false;
  }

  m_RailAccount.acquire(holder);
  const bool readBack = Sim::hwSetRail(simRailHolder(holder), true);
  m_RailOn = readBack;
  furble_sim_uart_set_rail(readBack);
  if (!readBack) {
    // Only drop a hold this call added; a pre-existing hold belongs to a
    // caller that still wants the rail.
    if (added) {
      (void)m_RailAccount.release(holder);
    }
    return false;
  }
  return true;
}

bool Platform::railRelease(Rail rail, RailHolder holder) {
  if (!railSwitchable(rail)) {
    return false;
  }

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  const RailAccount::release_t decision = m_RailAccount.release(holder);
  if (decision.ignored) {
    return !m_RailOn;
  }

  // Tell the model either way: it keeps the rail on while another holder
  // still needs it, which is the read-back the refusal is decided from.
  const bool readBack = Sim::hwSetRail(simRailHolder(holder), false);
  m_RailOn = readBack;
  furble_sim_uart_set_rail(readBack);
  if (readBack) {
    m_RailCutRefusals++;
    return false;
  }
  return true;
}

uint8_t Platform::railHolders(Rail rail) {
  if (!railSwitchable(rail)) {
    return 0;
  }

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  return m_RailAccount.mask();
}

bool Platform::readRailState(bool *on) {
  *on = Sim::hwRailOn();
  m_RailOn = *on;
  return true;
}

bool Platform::switchRailLocked(bool on) {
  // The host has no PMIC rail to observe, so the hardware model is the switch
  // and its read-back is the answer. `gps_rail_fixed` and `gps_rail_refused`
  // keep the rail on whatever the caller asked for.
  if (on && !railEnablePermitted(&m_RailVerdict)) {
    m_RailInterlockRefusals++;
    return false;
  }

  const bool readBack = Sim::hwSetRail(Sim::rail_holder_t::GPS, on);
  m_RailOn = readBack;
  furble_sim_uart_set_rail(readBack);
  return readBack == on;
}

uint32_t Platform::getBatteryFailCount(void) {
  return 0;
}

namespace Sim {

void setBoardProfile(m5::board_t board) {
  g_BoardProfile = board;
}

m5::board_t boardProfile(void) {
  return g_BoardProfile;
}

void captureBootSettings(const boot_settings_t &settings) {
  g_BootSettings = settings;
}

boot_settings_t bootSettings(void) {
  return g_BootSettings;
}

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
  platform.m_M5PM1.pollDeadlines();
  // An expiry is a PMIC-driven system power cycle, so the answer is what the
  // chip did, not a status bit it left behind.
  if (platform.m_M5PM1.watchdogPowerCycles() != 0) {
    return "cycled";
  }
  return platform.m_WatchdogEnabled ? "armed" : "disabled";
#else
  return "unavailable";
#endif
}

const char *motionWakeState(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  if (!platform.m_MotionWakeArmed) {
    return "idle";
  }
  // The motion interrupt reaches the SoC through the PMIC's aggregated IRQ line
  // (PYG1_IRQ into GPIO13). The line is level active, so a latch in Status 2
  // that the motion path never clears holds it asserted and a later edge cannot
  // wake anything.
  return platform.m_M5PM1.irqLineAsserted() ? "blocked" : "armed";
#else
  return "unavailable";
#endif
}

const char *pmicIrqState(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  return platform.m_M5PM1.irqLineAsserted() ? "asserted" : "idle";
#else
  return "unavailable";
#endif
}

const char *pmicIrqStatus(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  // The scenario thread formats and compares this value, so one buffer per
  // thread keeps the returned pointer valid without sharing mutable state.
  thread_local std::string status;
  status = std::to_string(platform.m_M5PM1.irqStatus1()) + ","
           + std::to_string(platform.m_M5PM1.irqStatus2());
  return status.c_str();
#else
  return "unavailable";
#endif
}

bool pmicInjectSystemIrq(uint8_t bits) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  platform.m_M5PM1.injectSysIrq(bits);
  return true;
#else
  (void)bits;
  return false;
#endif
}

bool pmicClearSystemIrq(void) {
#if defined(FURBLE_M5STICKS3)
  auto &platform = Platform::getInstance();
  // Write 0 to Status 2, which is the only thing that clears the latch.
  return platform.m5pm1Access([&platform]() { return platform.m_M5PM1.irqClearSysAll(); });
#else
  return false;
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

const char *displayOffState(void) {
  return hwDisplayOffState();
}

}  // namespace Sim
}  // namespace Furble

// The simulator's strong definition of the weak hook FurbleBoard.cpp declares:
// the row a scenario selected, if any.
extern "C" bool furble_sim_board_profile(m5::board_t *board) {
  if (Furble::Sim::boardProfile() == m5::board_t::board_unknown) {
    return false;
  }
  *board = Furble::Sim::boardProfile();
  return true;
}
