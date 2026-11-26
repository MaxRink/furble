#include <algorithm>
#include <cmath>

#include <esp_system.h>

#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <M5PM1.h>

#include "FurbleBatteryStatus.h"
#include "FurbleBoard.h"
#include "FurbleBoardCaps.h"
#include "FurbleBootTimeline.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleIP5306.h"
#include "FurblePlatform.h"
#include "FurblePlatformPolicy.h"
#include "FurblePmicIrq.h"
#include "FurblePower.h"
#include "FurblePowerPolicy.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleTypes.h"
#include "FurbleWatchdog.h"

namespace Furble {

bool Platform::m_Ip5306BoostHeld = true;

#if FURBLE_HAS_M5PM1
namespace {
using Watchdog::PM1_FEED_PERIOD_MS;
using Watchdog::PM1_TIMEOUT_S;
constexpr uint32_t TIMED_WAKE_MARKER = 0x49564c31;

// M5PM1 1.0.6, src/M5PM1.h m5pm1_pwr_cfg_t: PWR_CFG 0x06 bit 3 is
// BOOST_EN, the BOOST/GROVE (5VINOUT) enable that is the StickS3 EXT_5V port,
// and bit 4 is LED_CTRL, the status LED level. Both live in one byte, so both
// writes go through Platform::updatePowerConfig().
constexpr uint8_t PWR_CFG_EXT_5V = static_cast<uint8_t>(M5PM1_PWR_CFG_BOOST_EN);
constexpr uint8_t PWR_CFG_LED = static_cast<uint8_t>(M5PM1_PWR_CFG_LED_CTRL);

// Two write-and-read-back attempts: the first access after an I2C idle period
// only wakes the PMIC, and m5pm1Access() already retries once per transaction.
constexpr uint8_t RAIL_ATTEMPTS = 2;

// The interlock maps the vendor PWR_SRC enumeration onto the host-tested policy
// enum, so a vendor renumbering is a compile error rather than a silent
// reinterpretation. PWR_SRC is M5PM1_REG_PWR_SRC 0x04; 0x07 is HOLD_CFG, the
// power-hold configuration, so a "fix" that changed the constant would read the
// hold register, always see its low bits as USB_5V and silently disable this
// interlock while every test stayed green.
static_assert(static_cast<uint8_t>(PlatformPolicy::RailSupply::USB_5V)
                  == static_cast<uint8_t>(M5PM1_PWR_SRC_5VIN),
              "RailSupply::USB_5V must follow the M5PM1 PWR_SRC enumeration");
static_assert(static_cast<uint8_t>(PlatformPolicy::RailSupply::PORT_5V)
                  == static_cast<uint8_t>(M5PM1_PWR_SRC_5VINOUT),
              "RailSupply::PORT_5V must follow the M5PM1 PWR_SRC enumeration");
static_assert(static_cast<uint8_t>(PlatformPolicy::RailSupply::BATTERY)
                  == static_cast<uint8_t>(M5PM1_PWR_SRC_BAT),
              "RailSupply::BATTERY must follow the M5PM1 PWR_SRC enumeration");
static_assert(static_cast<uint8_t>(PlatformPolicy::RailSupply::UNKNOWN)
                  == static_cast<uint8_t>(M5PM1_PWR_SRC_UNKNOWN),
              "RailSupply::UNKNOWN must follow the M5PM1 PWR_SRC enumeration");

// The 32-byte RTC buffer holds the four byte wake marker and, after it, the
// wall-clock prediction TimeKeeper builds for the wake instant. The PM1 keeps
// the buffer while the ESP32 is off, so the prediction costs no flash write.
constexpr uint8_t TIMED_WAKE_RECORD_OFFSET = sizeof(TIMED_WAKE_MARKER);
constexpr uint8_t TIMED_WAKE_BUFFER_BYTES = 32;
static_assert(TIMED_WAKE_RECORD_OFFSET + TimeKeeperPolicy::RECORD_BYTES <= TIMED_WAKE_BUFFER_BYTES,
              "the timed wake prediction does not fit the M5PM1 RTC buffer");
// The marker and the prediction are cleared as one buffer: no later boot may
// pair one with the other's absence.
constexpr uint8_t TIMED_WAKE_CLEAR[TIMED_WAKE_BUFFER_BYTES] = {};
}  // namespace
#endif

namespace {
// BM8563 registers used to prove the timer was actually armed. M5Unified's
// setTimerIRQ() returns the duration its own arithmetic selected even when the
// underlying register writes fail, so the value alone is not evidence.
constexpr uint8_t PCF8563_ADDRESS = 0x51;
constexpr uint8_t PCF8563_CONTROL2 = 0x01;
constexpr uint8_t PCF8563_TIMER_CONTROL = 0x0E;
constexpr uint8_t PCF8563_TIMER_COUNT = 0x0F;
constexpr uint32_t PCF8563_I2C_FREQ = 400000;

void clearBm8563Timer(void) {
  // Timer control 0x03 disables the countdown. Clearing it prevents a failed
  // setup from waking the device after a later manual power off.
  (void)M5.In_I2C.writeRegister8(PCF8563_ADDRESS, PCF8563_TIMER_CONTROL, 0x03, PCF8563_I2C_FREQ);
}

bool verifyBm8563Timer(int requested_seconds, int programmed_seconds) {
  // Mirror setTimerIRQ()'s selection: second resolution below 270 s, minute
  // resolution above it, both clamped to the 8-bit countdown register.
  const bool minute_mode = requested_seconds >= 270;
  const uint32_t divisor = minute_mode ? 60U : 1U;
  const uint32_t raw_count = minute_mode ? (static_cast<uint32_t>(requested_seconds) + 30U) / 60U
                                         : static_cast<uint32_t>(requested_seconds);
  const uint8_t expected_count = static_cast<uint8_t>(std::min<uint32_t>(raw_count, 255U));
  const uint8_t expected_control = minute_mode ? 0x83 : 0x82;
  const int expected_programmed = static_cast<int>(expected_count) * static_cast<int>(divisor);

  const uint8_t control =
      M5.In_I2C.readRegister8(PCF8563_ADDRESS, PCF8563_TIMER_CONTROL, PCF8563_I2C_FREQ);
  const uint8_t count =
      M5.In_I2C.readRegister8(PCF8563_ADDRESS, PCF8563_TIMER_COUNT, PCF8563_I2C_FREQ);
  const uint8_t control2 =
      M5.In_I2C.readRegister8(PCF8563_ADDRESS, PCF8563_CONTROL2, PCF8563_I2C_FREQ);
  // The countdown can lose its first tick between the write and this read.
  const bool count_ok =
      (count == expected_count) || ((expected_count > 0) && (count + 1 == expected_count));
  return (control == expected_control) && count_ok && ((control2 & 0x01) != 0)
         && (programmed_seconds == expected_programmed);
}
}  // namespace

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    Power::init();
    instance.setCPUMaxFreq(CPU_MAX_FREQ_DEFAULT_MHZ);

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = Settings::load<Settings::IMU>();
    cfg.internal_spk = Feedback::outputIncludesSound(
        static_cast<Feedback::output_t>(Settings::load<uint8_t>(Settings::FB_OUTPUT)));
    cfg.internal_mic = false;
    cfg.pmic_button = true;
#if FURBLE_HAS_M5PM1
    // The rail manager owns this board's EXT_5V port. M5Unified's default
    // output_power is true and _begin() calls setExtOutput(cfg.output_power)
    // before any Furble code runs, which would commit the port to output mode
    // with no owner, no accounting and no supply interlock (plans/364). One
    // PWR_CFG 0x06 writer keeps the port in the vendor default of input mode
    // until a holder acquires it.
    //
    // The AXP boards keep M5Unified's own port control on purpose. Their
    // setExtOutput() switches EXTEN or LDOIO0 at begin() and refuses external
    // power on a low battery with no ACIN supply; the Grove 5 V output there has
    // no other owner, so clearing output_power would leave the port unpowered
    // from boot with no client that could switch it back on
    // (plans/364-review-feedback.md, B2).
    cfg.output_power = false;
#endif
    cfg.disable_rtc_irq = false;
#if !FURBLE_HAS_INTERNAL_I2C
    // No internal I2C bus, so M5Unified must not be given one. Its per-board
    // pin table decides that, and it reads the row from the board M5GFX
    // reports: with the M5GFX pin that row is `board_M5StampS3`, whose internal
    // I2C pins are both 255, so In_I2C gets the closed sentinel port and M5GFX
    // rejects every transaction on it. Its RTC and IMU drivers still call
    // begin() on that port, so turn them off here rather than let them ask for
    // a bus that does not exist. The unknown-board row would have put that bus
    // on GPIO 38/39, which are not an I2C bus on this board.
    cfg.internal_rtc = false;
    cfg.internal_imu = false;
#endif
    M5.begin(cfg);

    // A panel-off IP5306 board draws less than 45 mA, which the boost's
    // light-load cut treats as a request to remove power. Hold it on before
    // anything can dim the panel.
    instance.keepIp5306BoostOn();

    const auto &board = Board::profile();
    if (board.powerHoldPin >= 0) {
      const auto hold = static_cast<gpio_num_t>(static_cast<uint8_t>(board.powerHoldPin));
      (void)gpio_set_direction(hold, GPIO_MODE_OUTPUT);
      (void)gpio_set_level(hold, 1);
    }
    // Only a board with an RTC the driver can reach has a stale RTC IRQ to
    // clear, so the write is skipped outright when none is on the bus. On the
    // StickS3 M5Unified's PCF8563 fallback probe at 0x51 is not acknowledged
    // and no RTC is instantiated, which makes this call at best a no-op and,
    // if a future M5Unified stops gating on its probe result, an unneeded
    // NACKing transaction in the boot window. The Plus2 keeps its BM8563 alarm
    // flag for consumeTimedWake(), so it is excluded by the second test.
    if (Board::clearsStaleRtcAlarm(board)) {
      M5.Rtc.disableIRQ();
    }
    BootTimeline::mark(BootTimeline::DISPLAY_READY);

    // M5Unified enables every AXP192 ADC channel at 25 Hz. Trim the unused
    // channels, but only when the detected PMIC really is an AXP192: Core2 v1.1
    // keeps the board ID while carrying an AXP2101, where these registers are
    // regulator controls, not the ADC enable map.
    if (M5.Power.getType() == m5::Power_Class::pmic_axp192) {
      M5.In_I2C.writeRegister8(0x34, 0x82, PlatformPolicy::AXP192_ADC_ENABLE_1, 400000);
      M5.In_I2C.writeRegister8(0x34, 0x83, PlatformPolicy::AXP192_ADC_ENABLE_2, 400000);
    }

    // The IMU keeps the M5Unified default axis order. The spirit level derives
    // roll from atan2(ay, az) to drive screen X and pitch to drive screen Y.
    // That orientation is UNVERIFIED on all boards pending a hardware check.

    instance.m_PMICHack = board.pmicClickStreak;

#if FURBLE_HAS_M5PM1
    instance.m_M5PM1.begin(&M5.In_I2C);
    instance.m_M5PM1.setSingleResetDisable(true);  // disable BtnPWR single-click reset
    instance.m_M5PM1.setDoubleOffDisable(true);    // disable BtnPWR double-click power off
    // Never lock the only recovery path which works when the ESP32 is wedged.
    // The PMIC setting is retained independently of an ESP32 reset, so this
    // must be an explicit write followed by a readback on every boot.
    if (!instance.unlockDownloadRecovery()) {
      ESP_LOGE(LOG_TAG, "M5PM1 download recovery remains unavailable");
    }
#endif

    instance.initBattery();
    BootTimeline::mark(BootTimeline::PMIC_READY);

    instance.m_Init = true;
  }

  return instance;
}

void Platform::init(void) {
  (void)getInstance();
}

void Platform::prepareRestart(void) {
  TimeKeeper::getInstance().flush();
  auto &control = Control::getInstance();
  // forRestart == true: esp_restart() runs immediately after, so a force-
  // complete on timeout is safe here. The reset kills any in-flight BLE
  // teardown, so nothing can reconnect and race the still-freeing client.
  if (!control.disconnect(Control::DISCONNECT_TIMEOUT_MS, true)) {
    ESP_LOGW(LOG_TAG, "Restart continuing after camera disconnect timeout.");
  }

#if FURBLE_HAS_M5PM1
  watchdogEnable(false);
#endif
}

void Platform::restart(void) {
  prepareRestart();
  esp_restart();
}

#if FURBLE_HAS_M5PM1
bool Platform::unlockDownloadRecovery(void) {
  if (!m5pm1Access([this]() { return m_M5PM1.setDownloadLock(false); })) {
    ESP_LOGW(LOG_TAG, "Unable to unlock M5PM1 download recovery");
    return false;
  }

  bool locked = true;
  if (!m5pm1Access([this, &locked]() { return m_M5PM1.getDownloadLock(&locked); })) {
    ESP_LOGW(LOG_TAG, "Unable to verify M5PM1 download recovery");
    return false;
  }

  if (locked) {
    ESP_LOGE(LOG_TAG, "M5PM1 rejected download recovery unlock");
    return false;
  }

  ESP_LOGI(LOG_TAG, "M5PM1 long-press download recovery enabled");
  return true;
}

bool Platform::clearPmicIrqLatches(void) {
  // Both latches are cleared, not just the GPIO one the motion path reads. See
  // include/FurblePmicIrq.h for the register map and why the system status is
  // cleared rather than masked.
  return PmicIrq::clearLatched([this](PmicIrq::Status status) {
    return m5pm1Access([this, status]() {
      return (status == PmicIrq::Status::SYSTEM) ? m_M5PM1.irqClearSysAll()
                                                 : m_M5PM1.irqClearGpioAll();
    });
  });
}

bool Platform::prepareFlash(void) {
  // A serial upload can spend longer than the normal PMIC watchdog window in
  // ROM download mode. The caller must be a deliberate local console user;
  // the regular runtime watchdog is restored on the next application boot.
  if (!watchdogEnable(false)) {
    ESP_LOGE(LOG_TAG, "M5PM1 watchdog disable failed before flash");
    (void)watchdogEnable(true);
    return false;
  }
  uint8_t watchdogCount = 1;
  if (!m5pm1Access([this, &watchdogCount]() { return m_M5PM1.wdtGetCount(&watchdogCount); })
      || watchdogCount != 0) {
    ESP_LOGE(LOG_TAG, "M5PM1 watchdog did not verify disabled for flash");
    (void)watchdogEnable(true);
    return false;
  }

  if (!unlockDownloadRecovery()) {
    if (!watchdogEnable(true)) {
      ESP_LOGE(LOG_TAG, "M5PM1 watchdog restore failed after flash preparation error");
    }
    return false;
  }

  ESP_LOGW(LOG_TAG, "M5PM1 watchdog disabled for flash; upload now, or run 'flash cancel'");
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
  if (!watchdogEnable(true)) {
    ESP_LOGE(LOG_TAG, "M5PM1 watchdog restore failed after cancelled flash");
    return false;
  }
  ESP_LOGI(LOG_TAG, "M5PM1 watchdog restored after cancelled flash");
  return true;
}

bool Platform::watchdogEnable(bool enable) {
  m_WatchdogEnabled = false;
  m_WatchdogLastFeed = tick();

  const uint8_t timeout = enable ? PM1_TIMEOUT_S : 0;
  if (!m5pm1Access([this, timeout]() { return m_M5PM1.wdtSet(timeout); })) {
    ESP_LOGE(LOG_TAG, "Failed to set M5PM1 watchdog to %u seconds", static_cast<unsigned>(timeout));
    return false;
  }

  uint8_t count = enable ? 0 : 1;
  if (!m5pm1Access([this, &count]() { return m_M5PM1.wdtGetCount(&count); })
      || (enable ? count == 0 : count != 0)) {
    ESP_LOGE(LOG_TAG, "Failed to verify M5PM1 watchdog %s", enable ? "armed" : "disabled");
    return false;
  }

  if (enable) {
    m_WatchdogEnabled = true;
    ESP_LOGI(LOG_TAG, "M5PM1 watchdog armed for %u seconds", static_cast<unsigned>(PM1_TIMEOUT_S));
  } else {
    ESP_LOGI(LOG_TAG, "M5PM1 watchdog disabled");
  }
  return true;
}

void Platform::watchdogFeed(void) {
  if (!m_WatchdogEnabled) {
    return;
  }

  const uint32_t now = tick();
  if (now - m_WatchdogLastFeed < PM1_FEED_PERIOD_MS) {
    return;
  }
  m_WatchdogLastFeed = now;

  if (!m5pm1Access([this]() { return m_M5PM1.wdtFeed(); })) {
    ESP_LOGW(LOG_TAG, "Failed to feed M5PM1 watchdog");
  }
}

void Platform::wakeM5PM1(void) {
  uint16_t mv = 0;
  // The read result is discarded, m5pm1Access retries once so the PMIC is
  // awake when this returns.
  (void)m5pm1Access([this, &mv]() { return m_M5PM1.readVbat(&mv); });
}

void Platform::setDisplayOff(bool off) {
  // LED_CTRL (PWR_CFG 0x06 bit 4) shares its byte with the EXT_5V rail bit, so
  // every LED write goes through updatePowerConfig() as well. Reading the old
  // level and writing the new one used to be two independent driver calls,
  // which is how the rail and the LED could lose each other's update when the
  // GPS task switched the rail between them (plans/364).
  if (off) {
    if (m_StatusLedLevelValid) {
      return;
    }

    uint8_t before = 0;
    if (!updatePowerConfig(PWR_CFG_LED, 0, &before, nullptr)) {
      ESP_LOGW(LOG_TAG, "Unable to turn off StickS3 status LED");
      return;
    }

    m_StatusLedLevel = (before & PWR_CFG_LED) != 0;
    m_StatusLedLevelValid = true;
    return;
  }

  if (!m_StatusLedLevelValid) {
    return;
  }

  const uint8_t ledLevel = m_StatusLedLevel ? PWR_CFG_LED : 0;
  if (!updatePowerConfig(PWR_CFG_LED, ledLevel, nullptr, nullptr)) {
    ESP_LOGW(LOG_TAG, "Unable to restore StickS3 status LED level");
    return;
  }

  m_StatusLedLevelValid = false;
}
#endif

#if !FURBLE_HAS_M5PM1
// Boards without the M5PM1 have no hardware watchdog to feed.
void Platform::watchdogFeed(void) {}
#endif

uint32_t Platform::tick(void) {
  return (esp_timer_get_time() / 1000LL);
}

uint8_t Platform::getPWRClickCount(void) {
  uint8_t count = m_PMICClickCount;

  if (count > 0) {
    m_PMICClickCount = 0;
  }

  return count;
}

bool Platform::isCPUMaxFreqValid(uint8_t mhz) {
  return std::find(CPU_MAX_FREQ_MHZ.begin(), CPU_MAX_FREQ_MHZ.end(), mhz) != CPU_MAX_FREQ_MHZ.end();
}

void Platform::setCPUMaxFreq(uint8_t mhz) {
  uint8_t freq = mhz;

  // NVS may hold anything, never hand an unvetted value to esp_pm_configure()
  if (!isCPUMaxFreqValid(freq)) {
    ESP_LOGW(LOG_TAG, "Unsupported CPU maximum frequency %dMHz, using %dMHz.", freq,
             CPU_MAX_FREQ_DEFAULT_MHZ);
    freq = CPU_MAX_FREQ_DEFAULT_MHZ;
  }

  // The board may still refuse the frequency, fall back rather than abort
  auto &power = Power::getInstance();
  esp_err_t err = power.configure(freq);
  if (err != ESP_OK) {
    // ESP_ERR_NOT_SUPPORTED means this build has no esp_pm at all. That is a
    // configuration fact, not a fault: the board runs at its boot frequency.
    // Every other error is a bad argument or a driver failure and stays fatal
    // after the documented fallback.
    const bool notSupported = (err == ESP_ERR_NOT_SUPPORTED);
    ESP_LOGW(LOG_TAG, "CPU maximum frequency %dMHz rejected (%s, 0x%x), using %dMHz.", freq,
             esp_err_to_name(err), static_cast<unsigned>(err), CPU_MAX_FREQ_DEFAULT_MHZ);
    freq = CPU_MAX_FREQ_DEFAULT_MHZ;
    err = power.configure(freq);
    if (err != ESP_OK) {
      if (notSupported && (err == ESP_ERR_NOT_SUPPORTED)) {
        ESP_LOGW(LOG_TAG,
                 "Power management is not available in this build; running at a fixed "
                 "frequency.");
      } else {
        ESP_ERROR_CHECK(err);
      }
    }
  }

  m_CPUMaxFreqMHz = freq;

  ESP_LOGI(LOG_TAG, "CPU maximum frequency %dMHz.", m_CPUMaxFreqMHz);
}

uint8_t Platform::getCPUMaxFreq(void) const {
  return m_CPUMaxFreqMHz;
}

Platform::pm_config_t Platform::getPMConfig(void) {
  esp_pm_config_t pm_config = {};

  esp_err_t err = esp_pm_get_configuration(&pm_config);
  if (err != ESP_OK) {
    // fall back to the last configuration Power was asked for
    ESP_LOGW(LOG_TAG, "Unable to read power management configuration (%s, 0x%x).",
             esp_err_to_name(err), static_cast<unsigned>(err));
    return {m_CPUMaxFreqMHz, Power::CPU_MIN_FREQ_MHZ, true};
  }

  return {static_cast<uint8_t>(pm_config.max_freq_mhz),
          static_cast<uint8_t>(pm_config.min_freq_mhz), pm_config.light_sleep_enable};
}

void Platform::dumpPMLocks(void) {
  esp_err_t err = esp_pm_dump_locks(stdout);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "Unable to dump power management locks (%s, 0x%x).", esp_err_to_name(err),
             static_cast<unsigned>(err));
  }
}

bool Platform::canCutGpsRail(void) {
  return Board::profile().gpsRail;
}

bool Platform::canTurnDisplayOff(void) {
#if defined(CONFIG_IDF_TARGET_ESP32)
  if (M5.Power.getType() == m5::Power_Class::pmic_ip5306) {
    return m_Ip5306BoostHeld;
  }
#endif
  return true;
}

void Platform::keepIp5306BoostOn(void) {
#if defined(CONFIG_IDF_TARGET_ESP32)
  // Only the classic ESP32 M5Unified build carries the IP5306 member. The
  // Core is the only supported board whose PMIC type is pmic_ip5306.
  if (M5.Power.getType() != m5::Power_Class::pmic_ip5306) {
    return;
  }

  // M5Unified 0.2.20 writes SYS_CTL0 = 0b00110001 at M5.begin(), which clears
  // bit 1, the "BOOST output normally open" bit, and writes SYS_CTL2 =
  // 0b01101100, the 64 s light-load window. Without bit 1 the boost is cut
  // after that window whenever the load stays below 45 mA, and a panel-off
  // Core is below it. The IP5306 has no shutdown command, so there is no later
  // power-off path that should clear the bit: hold the boost on for the whole
  // session. Register meanings are from the Injoinic IP5306 register document
  // V1.21 (revision V1.4), SYS_CTL0 bit 1 and SYS_CTL2 bits 3:2.
  // Both log lines are short on purpose: this profile shares a 1,740,800-byte
  // OTA slot with the debug build of the same image, and the boot log is not
  // worth a build failure. The SYS_CTL2 window decode stays pinned by the
  // ip5306-policy host test rather than printed here.
  if (!IP5306::holdBoostOn(M5.Power.Ip5306)) {
    m_Ip5306BoostHeld = false;
    ESP_LOGE(LOG_TAG, "IP5306 keep-on failed, display-off gated to Dim");
    return;
  }
  const uint8_t sysCtl2 = M5.Power.Ip5306.readRegister8(IP5306::REG_SYS_CTL2);
  ESP_LOGI(LOG_TAG, "IP5306 keep-on set (SYS_CTL2=0x%02x)", static_cast<unsigned>(sysCtl2));
#endif
}

bool Platform::hasGpsBackupSupply(void) {
#if defined(FURBLE_GPS_BACKUP_SUPPLY)
  return true;
#else
  return false;
#endif
}

bool Platform::hasTicklessIdle(void) {
#if defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
  return true;
#else
  return false;
#endif
}

bool Platform::powerOff(void) {
  // Refuse before any teardown. TimeKeeper::flush() is harmless, but the SD
  // unmount is one way: a caller that must stay alive would be left without its
  // card. See plans/349-waveshare-board-row.md.
  if (PlatformPolicy::powerOffAction(Board::profile().powerOffPath)
      == PlatformPolicy::PowerOffAction::REFUSE) {
    ESP_LOGW(LOG_TAG, "Power off refused, the %s profile has no power-off path",
             Board::profile().name);
    return false;
  }

  TimeKeeper::getInstance().flush();
  // the SD writer task closes the track and unmounts, wait for it to finish
  if (!SD::getInstance().powerOff()) {
    // Cutting the rail with the track still open loses its tail and leaves the
    // filesystem dirty. Leave the watchdog as the user set it and stay alive.
    ESP_LOGW(LOG_TAG, "SD writer did not acknowledge, refusing to power off");
    return false;
  }

#if FURBLE_HAS_M5PM1
  const bool wasArmed = m_WatchdogEnabled;
  watchdogEnable(false);

  // The first M5PM1 access after its idle sleep fails and only wakes it, so
  // go through the retry helper. On success the rail drops mid-call.
  if (m5pm1Access([this]() { return m_M5PM1.shutdown(); })) {
    return true;
  }

  ESP_LOGW(LOG_TAG, "M5PM1 shutdown refused, staying alive");
  if (wasArmed) {
    watchdogEnable(true);
  }
  return false;
#else
  // M5.Power.powerOff() sleeps the panel, clears the IP5306 boost keep-on bit
  // when the PMIC is one, and then deep sleeps. On the Core that clear is the
  // cut: the sleeping load falls under the 45 mA light-load threshold and the
  // charger removes the boost after its SYS_CTL2 window. On an AXP192 board the
  // PMIC cuts the rail outright. Neither returns, so a return here means the
  // PMIC refused.
  M5.Power.powerOff();
  return true;
#endif
}

#if !FURBLE_HAS_M5PM1
void Platform::setDisplayOff(bool off) {
  (void)off;
}
#endif

namespace {

/**
 * The ESP32 pin the IMU interrupt reaches on this board, or GPIO_NUM_NC.
 *
 * M5StickC and M5StickC Plus: GPIO35 carries one open-drain active-low net with
 * three sources: the MPU6886 INT, the BM8563 INT (the RTC alarm and countdown),
 * and the AXP192 IRQ output. M5Unified's AXP192 init leaves the power-key IRQ
 * enabled (IRQ enable 3, reg 0x42 = 0x03), so a power-key press also pulls the
 * net low until its status is read. GPIO35 is input only on the ESP32 and has
 * no internal pull of any kind, so it relies entirely on the board's external
 * pull-up.
 *
 * A low level therefore cannot be attributed to one source. Masking the AXP192
 * source is not an option: the same enable bit produces the power-key status in
 * reg 0x46 that M5Unified reads for M5.BtnPWR, and `Platform::update()` builds
 * the PMIC click streak from that button, so the power key would go dead while
 * motion wake is armed. Every low level is treated as "the device should be
 * awake": correct for motion, and the intended response to a button press as
 * well. That is also why the hardware gate reads the line instead of trusting
 * it as an IMU-only signal.
 *
 * M5StickS3: the BMI270 INT1 does not reach the SoC. It goes to M5PM1 GPIO4,
 * and the PMIC drives PYG1_IRQ into GPIO13. That pin is a normal GPIO and does
 * take an internal pull-up, which is configured below.
 */
// Counted from a latched flag, never from a GPIO interrupt.
//
// The first version of this installed gpio_set_intr_type(GPIO_INTR_ANYEDGE) on
// the wake pin. That silently disabled the wake source: gpio_set_intr_type and
// gpio_wakeup_enable both write hw->pin[n].int_type, and the IDF refuses an
// edge type on a wakeup pin ("GPIO wakeup only supports level mode"), so the
// counter cancelled the level wake it existed to measure. Nothing may touch the
// wake pin's interrupt type.
//
// Both boards already latch the event somewhere the poll can read without
// touching the pin configuration: the M5StickS3 in the PMIC's GPIO IRQ status,
// and the StickC family in the MPU6886's own latched interrupt, which holds the
// line until the status register is read.
uint32_t g_MotionWakeEdges = 0;

/** The board's motion wake path, NONE when this image cannot drive it. */
Board::MotionWake motionWakeKind(void) {
  const auto kind = Board::profile().motionWake;
  // The PMIC path needs the M5PM1 driver, which only its image carries.
  if ((kind == Board::MotionWake::PMIC_GPIO) && !FURBLE_HAS_M5PM1) {
    return Board::MotionWake::NONE;
  }
  return kind;
}

gpio_num_t motionWakeGpio(void) {
  if (motionWakeKind() == Board::MotionWake::NONE) {
    return GPIO_NUM_NC;
  }
  return static_cast<gpio_num_t>(Board::profile().motionWakePin);
}

}  // namespace

bool Platform::armMotionWake(void) {
  if (m_MotionWakeArmed) {
    return true;
  }

  gpio_num_t gpio = GPIO_NUM_NC;
  switch (motionWakeKind()) {
    case Board::MotionWake::DIRECT:
      gpio = motionWakeGpio();
      break;

#if FURBLE_HAS_M5PM1
    case Board::MotionWake::PMIC_GPIO:
    {
      // Clear both IRQ status latches before the level wake is armed. The
      // M5PM1 holds its aggregated IRQ line low until the IRQ Status is
      // cleared, and Status 2 carries 5VIN insert and remove, which nothing
      // here consumes. Clearing only Status 1 is what left a USB insert or
      // removal asserting the line for the rest of the session.
      if (!m5pm1Access([this]() {
            return m_M5PM1.gpioSet(M5PM1_GPIO_NUM_4, M5PM1_GPIO_MODE_INPUT, 0, M5PM1_GPIO_PULL_UP,
                                   M5PM1_GPIO_DRIVE_PUSHPULL);
          })
          || !m5pm1Access(
              [this]() { return m_M5PM1.gpioSetFunc(M5PM1_GPIO_NUM_4, M5PM1_GPIO_FUNC_IRQ); })
          || !clearPmicIrqLatches() || !m5pm1Access([this]() {
               return m_M5PM1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_DISABLE);
             })
          || !m5pm1Access([this]() { return m_M5PM1.gpioSetWakeEnable(M5PM1_GPIO_NUM_4, true); })
          || !m5pm1Access([this]() {
               return m_M5PM1.gpioSetWakeEdge(M5PM1_GPIO_NUM_4, M5PM1_GPIO_WAKE_FALLING);
             })) {
        return false;
      }

      gpio = motionWakeGpio();
      break;
    }
#endif

    default:
      return false;
  }

  if (gpio == GPIO_NUM_NC) {
    return false;
  }

  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << static_cast<uint32_t>(gpio);
  config.mode = GPIO_MODE_INPUT;
  // Every source on this net is open-drain active low, so the line needs a
  // pull-up to return to idle. GPIO35 on the StickC family is input only with
  // no internal pull and depends on the board's external pull-up on the shared
  // net that carries the IMU INT, the BM8563 INT and the AXP192 IRQ; every
  // other pin here takes the internal one.
  config.pull_up_en = (gpio == GPIO_NUM_35) ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  if ((gpio_config(&config) == ESP_OK) && (gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL) == ESP_OK)
      && (esp_sleep_enable_gpio_wakeup() == ESP_OK)) {
    g_MotionWakeEdges = 0;
    m_MotionWakeArmed = true;
    return true;
  }
#if FURBLE_HAS_M5PM1
  if (motionWakeKind() == Board::MotionWake::PMIC_GPIO) {
    m5pm1Access([this]() { return m_M5PM1.gpioSetWakeEnable(M5PM1_GPIO_NUM_4, false); });
    m5pm1Access(
        [this]() { return m_M5PM1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_ENABLE); });
  }
#endif
  return false;
}

void Platform::disarmMotionWake(void) {
  if (!m_MotionWakeArmed) {
    return;
  }

  gpio_num_t gpio = GPIO_NUM_NC;
  switch (motionWakeKind()) {
    case Board::MotionWake::DIRECT:
      gpio = motionWakeGpio();
      break;
#if FURBLE_HAS_M5PM1
    case Board::MotionWake::PMIC_GPIO:
      gpio = motionWakeGpio();
      m5pm1Access([this]() { return m_M5PM1.gpioSetWakeEnable(M5PM1_GPIO_NUM_4, false); });
      m5pm1Access(
          [this]() { return m_M5PM1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_ENABLE); });
      // Both latches, so a 5VIN event that arrived while armed does not leave
      // the line asserted after the wake source is gone.
      (void)clearPmicIrqLatches();
      break;
#endif
    default:
      break;
  }

  if (gpio != GPIO_NUM_NC) {
    gpio_wakeup_disable(gpio);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  m_MotionWakeArmed = false;
}

bool Platform::motionWakeAsserted(void) const {
  if (!m_MotionWakeArmed) {
    return false;
  }

#if FURBLE_HAS_M5PM1
  if (motionWakeKind() == Board::MotionWake::PMIC_GPIO) {
    // The BMI270 interrupt never reaches the SoC. GPIO13 carries the PMIC's
    // aggregated IRQ, so reading it answers "did the PMIC raise something",
    // not "is the IMU asserting". The IMU line itself is M5PM1 GPIO4, so read
    // that: it is what gate steps 1 and 2 are actually asking about.
    uint8_t level = 1;
    auto *self = const_cast<Platform *>(this);
    if (!self->m5pm1Access(
            [self, &level]() { return self->m_M5PM1.gpioGetInput(M5PM1_GPIO_NUM_4, &level); })) {
      return false;
    }
    return level == 0;
  }
#endif

  const gpio_num_t gpio = motionWakeGpio();
  if (gpio == GPIO_NUM_NC) {
    return false;
  }
  // Active low: asserted is a zero on the pin.
  return gpio_get_level(gpio) == 0;
}

bool Platform::motionWakeSample(void) {
  if (!m_MotionWakeArmed) {
    return false;
  }

#if FURBLE_HAS_M5PM1
  if (motionWakeKind() == Board::MotionWake::PMIC_GPIO) {
    // The PMIC latches a GPIO4 edge in its IRQ status. Read and clear it: the
    // flag is what makes a fast INT1 train visible to a one-second poll, and
    // clearing it is also what stops a latched status from holding PYG1_IRQ
    // asserted and blocking light sleep.
    uint8_t status = 0;
    if (!m5pm1Access(
            [this, &status]() { return m_M5PM1.irqGetGpioStatus(&status, M5PM1_CLEAN_ONCE); })) {
      return false;
    }
    // Status 1 is not the only latch that holds PYG1_IRQ low. A 5VIN insert or
    // removal latches Status 2, which nothing consumes, and the line then
    // stays asserted while this poll keeps reporting an idle IMU. Clear it
    // here as well; Status 1 is left to the read above so an edge that arrives
    // between the two accesses is not discarded.
    (void)m5pm1Access([this]() { return m_M5PM1.irqClearSysAll(); });
    if ((status & M5PM1_IRQ_GPIO4) == 0) {
      return false;
    }
    g_MotionWakeEdges++;
    return true;
  }
#endif

  const gpio_num_t gpio = motionWakeGpio();
  if (gpio == GPIO_NUM_NC) {
    return false;
  }
  // The MPU6886 interrupt is latched in the sensor, so the line holds until the
  // status register is read and a poll-rate sample is meaningful. Active low,
  // and shared with the BM8563 and the AXP192 IRQ, so this counts a button press
  // as activity too. See the wake pin comment above for why that is intended.
  if (gpio_get_level(gpio) != 0) {
    return false;
  }
  g_MotionWakeEdges++;
  return true;
}

uint32_t Platform::motionWakeEdges(void) const {
  return g_MotionWakeEdges;
}

uint32_t Platform::getM5PM1RetryCount(void) const {
  return m_M5PM1RetryCount;
}

void Platform::clearMotionWake(void) {
  if (!m_MotionWakeArmed) {
    return;
  }
#if FURBLE_HAS_M5PM1
  if (motionWakeKind() == Board::MotionWake::PMIC_GPIO) {
    // m5pm1Access retries once, which is the documented behaviour of the first
    // transaction after the PMIC's idle sleep, and a wake is exactly when that
    // first transaction happens.
    (void)clearPmicIrqLatches();
  }
#endif
}

bool Platform::canTimedWake(void) {
  switch (Board::profile().timedWake) {
    case Board::TimedWake::PMIC_TIMER:
      // the timer is driven through the M5PM1 driver, only its image has it
      return FURBLE_HAS_M5PM1 != 0;
    case Board::TimedWake::RTC_ALARM:
      return M5.Rtc.isEnabled();
    default:
      return false;
  }
}

bool Platform::powerOffUntil(uint32_t seconds) {
  if (seconds == 0) {
    return powerOff();
  }

  // A recent GPS or NTP correction may still be inside the RTC write interval.
  // Checkpoint it before a timed shutdown so the resumed deadline is not based
  // on an older RTC value.
  TimeKeeper::getInstance().flush();

#if FURBLE_HAS_M5PM1
  if (Board::profile().timedWake == Board::TimedWake::PMIC_TIMER) {
    const bool watchdogWasEnabled = m_WatchdogEnabled;
    const auto restoreWatchdog = [this, watchdogWasEnabled]() {
      if (watchdogWasEnabled && !watchdogEnable(true)) {
        ESP_LOGE(LOG_TAG, "Failed to restore M5PM1 watchdog after timed wake setup failure");
      }
    };

    if (!watchdogEnable(false)) {
      // A partial disable can leave an armed watchdog that nobody feeds, or a
      // disabled watchdog on an awake device. Restore the pre-call state.
      restoreWatchdog();
      return false;
    }

    if (!m5pm1Access(
            [this, seconds]() { return m_M5PM1.timerSet(seconds, M5PM1_TIM_ACTION_POWERON); })) {
      ESP_LOGE(LOG_TAG, "Failed to set M5PM1 wake timer");
      // timerSet() arms before its final reload write, so a reported failure
      // can still leave a timer running. Clear it before returning or a later
      // manual shutdown could wake unexpectedly.
      if (!m5pm1Access([this]() { return m_M5PM1.timerClear(); })) {
        ESP_LOGE(LOG_TAG, "Failed to clear M5PM1 wake timer after setup failure");
      }
      restoreWatchdog();
      return false;
    }

    const uint32_t marker = TIMED_WAKE_MARKER;
    if (!m5pm1Access([this, &marker]() {
          return m_M5PM1.writeRtcRAM(0, reinterpret_cast<const uint8_t *>(&marker), sizeof(marker));
        })) {
      ESP_LOGE(LOG_TAG, "Failed to save M5PM1 wake marker");
      (void)m5pm1Access([this]() { return m_M5PM1.timerClear(); });
      restoreWatchdog();
      return false;
    }

    // The SD writer may still hold an open track. Unmount before the PMIC
    // removes the rail, and refuse the cut when it does not acknowledge, as the
    // manual path does (#494): a wedged writer means an open track.
    if (!SD::getInstance().powerOff()) {
      ESP_LOGW(LOG_TAG, "SD writer did not acknowledge, staying awake");
      (void)m5pm1Access([this]() { return m_M5PM1.timerClear(); });
      (void)m5pm1Access(
          [this]() { return m_M5PM1.writeRtcRAM(0, TIMED_WAKE_CLEAR, sizeof(TIMED_WAKE_CLEAR)); });
      restoreWatchdog();
      return false;
    }

    // Predict the wall clock at the wake instant into the PM1 RTC buffer,
    // beside the marker. It survives the ESP power-off the PMIC performs and
    // costs no NVS write, so a timelapse cycle cannot turn a timed wake into
    // flash wear. A prediction that cannot be made is not a reason to refuse
    // the sleep: the next boot then keeps the stored checkpoint clock, which is
    // what every board did before this record existed.
    uint8_t prediction[TimeKeeperPolicy::RECORD_BYTES] = {};
    const size_t predictionLength =
        TimeKeeper::getInstance().prepareTimedWake(seconds, prediction, sizeof(prediction));
    if ((predictionLength > 0) && !m5pm1Access([this, &prediction, predictionLength]() {
          return m_M5PM1.writeRtcRAM(TIMED_WAKE_RECORD_OFFSET, prediction,
                                     static_cast<uint8_t>(predictionLength));
        })) {
      ESP_LOGW(LOG_TAG, "Failed to save the timed wake clock prediction");
    }

    if (!m5pm1Access([this]() { return m_M5PM1.shutdown(); })) {
      ESP_LOGE(LOG_TAG, "Failed to shut down M5PM1 for timed wake");
      (void)m5pm1Access([this]() { return m_M5PM1.timerClear(); });
      (void)m5pm1Access(
          [this]() { return m_M5PM1.writeRtcRAM(0, TIMED_WAKE_CLEAR, sizeof(TIMED_WAKE_CLEAR)); });
      restoreWatchdog();
      return false;
    }
    return true;
  }
#endif

  if (Board::profile().timedWake == Board::TimedWake::RTC_ALARM) {
    if (!M5.Rtc.isEnabled()) {
      ESP_LOGW(LOG_TAG, "%s RTC is unavailable, staying awake", Board::profile().name);
      return false;
    }

    M5.Rtc.disableIRQ();
    M5.Rtc.clearIRQ();
    const int requested_seconds = static_cast<int>(seconds);
    const int programmed_seconds = M5.Rtc.setAlarmIRQ(requested_seconds);
    // The BM8563 has one-second resolution up to 255 seconds and minute
    // resolution above that. A late wake would miss the saved shot deadline,
    // so reject anything above the request. An early wake is safe only within
    // the resume window: the 255-minute clamp can otherwise wake hours before
    // the deadline and the resume validation clears the run.
    const int early_seconds = requested_seconds - programmed_seconds;
    if ((programmed_seconds <= 0) || (early_seconds < 0)
        || (early_seconds > static_cast<int>(TIMED_WAKE_MAX_EARLY_S))
        || !verifyBm8563Timer(requested_seconds, programmed_seconds)) {
      ESP_LOGW(LOG_TAG, "%s RTC could not program %d seconds within the resume window (got %d)",
               Board::profile().name, requested_seconds, programmed_seconds);
      clearBm8563Timer();
      M5.Rtc.disableIRQ();
      return false;
    }

    if (programmed_seconds != requested_seconds) {
      ESP_LOGI(LOG_TAG, "%s RTC will wake %d seconds early", Board::profile().name,
               requested_seconds - programmed_seconds);
    }

    if (Board::profile().powerHoldPin < 0) {
      // AXP192 boards cannot cut their own rail while VBUS is present.
      // M5Unified would fall back to a deep sleep with no wake source, and the
      // BM8563 INT only reaches the PMIC power key, not an ESP32 wake pin.
      // Stay awake instead of stranding the unit on USB power.
      if (M5.Power.getVBUSVoltage() > 0) {
        ESP_LOGW(LOG_TAG, "%s on VBUS, RTC cut refused", Board::profile().name);
        clearBm8563Timer();
        M5.Rtc.disableIRQ();
        return false;
      }
    }

    M5.Display.sleep();

    const auto &board = Board::profile();

    // The SD writer may still hold an open track. The manual power-off path
    // unmounts before cutting power; a timed shutdown must do the same so a
    // board with a card in the slot does not lose its filesystem on the way
    // down. It is done after every refusal above, so a refused timed cut leaves
    // the card mounted and the device usable, and a writer that never
    // acknowledges refuses the cut the same way (#494).
    if (!SD::getInstance().powerOff()) {
      ESP_LOGW(LOG_TAG, "SD writer did not acknowledge, RTC cut refused");
      clearBm8563Timer();
      M5.Rtc.disableIRQ();
      return false;
    }

    if (board.powerHoldPin >= 0) {
      // The Plus2 releases the pin that keeps its regulator enabled.
      const auto hold = static_cast<gpio_num_t>(static_cast<uint8_t>(board.powerHoldPin));
      (void)gpio_set_direction(hold, GPIO_MODE_OUTPUT);
      (void)gpio_set_level(hold, 0);
      return true;
    }

    // No hold pin: the StickC Plus reaches sleep by a full AXP192 shutdown, and
    // its wake is the BM8563 INT coupled into the AXP192 power key through R401
    // and C401 (UNVERIFIED; the schematic was not available, plan 350). The
    // Tough reaches it the same way, with BM8563 INT on the AXP192 PWR pin in
    // the vendor pinmap and BT1 keeping the RTC counting while the PMIC is off.
    // M5Unified's AXP192 path cuts the rail and then deep sleeps, so this only
    // returns when the PMIC refused.
    M5.Power.powerOff();

    // M5Unified returned, so no cut happened and the device is still awake with
    // the alarm armed. Disarm it: the BM8563 INT is coupled to the power key on
    // these boards, so a stale alarm would press that key long after the user
    // asked for nothing.
    ESP_LOGE(LOG_TAG, "%s PMIC off returned, RTC disarmed", board.name);
    clearBm8563Timer();
    M5.Rtc.disableIRQ();
    return false;
  }

  ESP_LOGW(LOG_TAG, "Timed wake is unsupported on this board");
  return false;
}

bool Platform::consumeTimedWake(void) {
#if FURBLE_HAS_M5PM1
  if (Board::profile().timedWake == Board::TimedWake::PMIC_TIMER) {
    uint8_t wakeSource = 0;
    if (!m5pm1Access([this, &wakeSource]() {
          return m_M5PM1.getWakeSource(&wakeSource, M5PM1_CLEAN_ONCE);
        })) {
      return false;
    }

    uint32_t marker = 0;
    if (!m5pm1Access([this, &marker]() {
          return m_M5PM1.readRtcRAM(0, reinterpret_cast<uint8_t *>(&marker), sizeof(marker));
        })) {
      return false;
    }

    const bool timedWake =
        ((wakeSource & M5PM1_WAKE_SRC_TIM) != 0) && (marker == TIMED_WAKE_MARKER);

    // Read the prediction the shutdown left beside the marker, then clear the
    // whole buffer so a second boot cannot adopt the same record. Only a
    // confirmed timed wake makes the interval in it the one that elapsed; any
    // other boot discards it and keeps the stored checkpoint clock.
    uint8_t prediction[TimeKeeperPolicy::RECORD_BYTES] = {};
    if (!m5pm1Access([this, &prediction]() {
          return m_M5PM1.readRtcRAM(TIMED_WAKE_RECORD_OFFSET, prediction,
                                    static_cast<uint8_t>(sizeof(prediction)));
        })) {
      return false;
    }
    (void)m5pm1Access(
        [this]() { return m_M5PM1.writeRtcRAM(0, TIMED_WAKE_CLEAR, sizeof(TIMED_WAKE_CLEAR)); });
    if (timedWake && !TimeKeeper::getInstance().adoptTimedWake(prediction, sizeof(prediction))) {
      ESP_LOGW(LOG_TAG, "Timed wake kept the stored clock");
    }
    return timedWake;
  }
#endif

  if (Board::profile().timedWake == Board::TimedWake::RTC_ALARM) {
    const bool timedWake = M5.Rtc.isEnabled() && M5.Rtc.getIRQstatus();
    M5.Rtc.disableIRQ();
    return timedWake;
  }

  return false;
}

void Platform::initBattery(void) {
  // Capabilities follow the PMIC; see the rows in src/FurbleBoard.cpp.
  const auto &board = Board::profile();
  m_BatteryCaps = board.battery;
  m_BatteryCapacity = board.batteryCapacityMah;

  ESP_LOGI(LOG_TAG, "Battery: level=%u voltage=%u current=%u charging=%u capacity=%umAh",
           m_BatteryCaps.level, m_BatteryCaps.voltage, m_BatteryCaps.current,
           m_BatteryCaps.charging, m_BatteryCapacity);
}

Platform::battery_caps_t Platform::getBatteryCaps(void) {
  return m_BatteryCaps;
}

uint16_t Platform::getBatteryCapacity(void) {
  return m_BatteryCapacity;
}

bool Platform::railSwitchable(Rail rail) {
  return (rail == Rail::EXT_5V) && Board::profile().gpsRail;
}

bool Platform::setRail(Rail rail, bool on) {
  if (!railSwitchable(rail)) {
    return false;
  }
  // The external 5 V output. On the Core2 and Tough this is one BUS_5V net
  // shared by Grove Port A and the M-Bus, so a GPS rail cut also drops every
  // other 5 V consumer on those connectors. The request is advisory: M5Unified
  // refuses an enable with no ACIN, VBUS current present and a battery at or
  // below 8 percent. The M5PM1 sleeps after an I2C idle period and the first
  // access after that only wakes it, so read back and retry once. The final
  // read tells the GPS driver whether a cut took: a receiver stopped for the
  // cut is restarted when the rail stays on.

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  if (on && !m_RailAccount.held()) {
    // A switch-on with no holder would power EXT_5V with nobody accounting for
    // it, so the rail would stay up until the next boot. Clients take a hold
    // through railAcquire() instead; this entry point only re-asserts a rail a
    // holder already owns (plans/364-review-feedback.md, N4).
    ESP_LOGD(LOG_TAG, "Rail enable refused, no holder owns EXT_5V");
    return false;
  }

  if (!on && m_RailAccount.held()) {
    // Another holder needs the rail: the cut is refused and reported as one.
    // A GPS duty-cycle RAIL_OFF must not cut the infrared circuit mid-burst or
    // brown out a Grove device (plans/364).
    m_RailCutRefusals++;
    ESP_LOGW(LOG_TAG, "Rail cut refused, holders 0x%02x still need EXT_5V (refusals %lu)",
             static_cast<unsigned>(m_RailAccount.mask()),
             static_cast<unsigned long>(m_RailCutRefusals));
    return false;
  }

  return switchRailLocked(on);
}

bool Platform::railAcquire(Rail rail, RailHolder holder) {
  if (!railSwitchable(rail)) {
    return false;
  }

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  if (m_RailAccount.holds(holder) && m_RailOn) {
    // Already counted and powered: nothing to write, so a repeated acquire from
    // one task cannot inflate the count.
    return true;
  }

  const bool wasHeld = m_RailAccount.held();
  const bool added = !m_RailAccount.holds(holder);
  m_RailAccount.acquire(holder);
  if (wasHeld && m_RailOn) {
    // Another holder owns the powered rail. Verify it rather than trusting the
    // cached state, because a PMIC cut that failed earlier leaves it on with
    // the cache already updated, and the reverse is what a lost update looks
    // like.
    bool on = false;
    if (readRailState(&on) && on) {
      return true;
    }
  }

  if (!switchRailLocked(true)) {
    // No power. Drop the hold only when this call added it: a hold that was
    // already counted belongs to an earlier, still wanted acquire, and dropping
    // it here would strand the rail off for a caller that never asked again.
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
    // Not a holder: report the rail's real state rather than claiming a cut.
    return !m_RailOn;
  }

  if (!decision.cut) {
    // Another holder still needs the rail, so the cut is refused. The rail
    // stays on and the caller is told so; the GPS driver restarts a receiver it
    // stopped for a cut that did not take (include/FurbleGPS.h).
    m_RailCutRefusals++;
    ESP_LOGI(LOG_TAG, "Rail held by 0x%02x after holder %u released",
             static_cast<unsigned>(m_RailAccount.mask()), static_cast<unsigned>(holder));
    return false;
  }

  return switchRailLocked(false);
}

uint8_t Platform::railHolders(Rail rail) {
  if (!railSwitchable(rail)) {
    return 0;
  }

  const std::lock_guard<std::mutex> guard(m_RailMutex);
  return m_RailAccount.mask();
}

bool Platform::switchRailLocked(bool on) {
#if FURBLE_HAS_M5PM1
  // Off is always allowed: input mode is the vendor default for EXT_5V and the
  // safe direction. On commits the port to output mode, which the M5Stack
  // documentation restricts to a board powered through USB or 5VIN, so the
  // interlock runs first and a refusal leaves PWR_CFG 0x06 untouched.
  if (on && !railOutputPermitted()) {
    return false;
  }

  // Two attempts: the M5PM1 sleeps after an I2C idle period and the first
  // access after that only wakes it, and m5pm1Access() already retries once
  // per transaction. The read inside updatePowerConfig() is the verdict.
  for (uint8_t attempt = 0; attempt < RAIL_ATTEMPTS; attempt++) {
    uint8_t after = 0;
    if (updatePowerConfig(PWR_CFG_EXT_5V, on ? PWR_CFG_EXT_5V : 0, nullptr, &after)
        && (((after & PWR_CFG_EXT_5V) != 0) == on)) {
      m_RailOn = on;
      return true;
    }
  }

  // Report what the PMIC really holds, never the request. A receiver stopped
  // for a cut that did not take is restarted by the caller on this false.
  uint8_t config = 0;
  m_RailOn = readPowerConfig(&config) && ((config & PWR_CFG_EXT_5V) != 0);
  ESP_LOGW(LOG_TAG, "EXT_5V %s did not take, PWR_CFG holds it %s", on ? "on" : "off",
           m_RailOn ? "on" : "off");
  return false;
#else
  // AXP192 EXTEN (StickC family) and AXP2101 BLDO2 on a Core2 v1.1, through
  // M5Unified's setExtOutput(), which owns that register alone and refuses
  // external power on a low battery with no ACIN. There is no second writer of
  // that byte, so nothing to serialize here and no PWR_CFG interlock to apply.
  // The vendor pinmap lists 5V GROVE as an AXP192 output on the StickC and Plus,
  // so EXTEN does reach the Grove 5 V pin (plans/364, plans/386).
  M5.Power.setExtOutput(on, m5::ext_PA);
  if (M5.Power.getExtOutput() != on) {
    M5.Power.setExtOutput(on, m5::ext_PA);
  }
  m_RailOn = M5.Power.getExtOutput() == on;
  return m_RailOn;
#endif
}

#if FURBLE_HAS_M5PM1
bool Platform::readPowerConfig(uint8_t *value) {
  const std::lock_guard<std::mutex> guard(m_PowerConfigMutex);
  return m5pm1Access([this, value]() { return m_M5PM1.getPowerConfig(value); });
}

bool Platform::updatePowerConfig(uint8_t mask, uint8_t value, uint8_t *before, uint8_t *after) {
  const std::lock_guard<std::mutex> guard(m_PowerConfigMutex);

  // The read and the write are one critical section: the LED policy and the
  // rail policy both modify this byte, and an interval that spans another
  // writer's read-modify-write loses that update (plans/364).
  uint8_t current = 0;
  if (!m5pm1Access([this, &current]() { return m_M5PM1.getPowerConfig(&current); })) {
    return false;
  }
  if (before != nullptr) {
    *before = current;
  }

  // M5PM1 1.0.6 setPowerConfig() performs its own masked read-modify-write of
  // PWR_CFG 0x06, so this stays the single write path for the byte.
  if (!m5pm1Access([this, mask, value]() { return m_M5PM1.setPowerConfig(mask, value); })) {
    return false;
  }

  uint8_t written = 0;
  if (!m5pm1Access([this, &written]() { return m_M5PM1.getPowerConfig(&written); })) {
    return false;
  }
  if (after != nullptr) {
    *after = written;
  }
  return true;
}

bool Platform::railOutputPermitted(void) {
  // The M5Stack EXT_5V note: the port defaults to INPUT, and in output mode
  // power may only be applied through USB or 5VIN, otherwise there is a risk of
  // short circuit and device damage (plans/364-ext5v-rail.md quotes it). The
  // port itself cannot be sensed, so the interlock refuses while an external
  // 5 V source is present at the bidirectional port or while the supply state
  // cannot be established. The failure direction is closed: a refusal leaves
  // PWR_CFG untouched and setRail() reports false.
  m5pm1_pwr_src_t source = M5PM1_PWR_SRC_UNKNOWN;
  const bool known = m5pm1Access([this, &source]() { return m_M5PM1.getPowerSource(&source); });
  // M5PM1 1.0.6 getPowerSource() returns PWR_SRC 0x04 bits 2:0 unclamped, so a
  // register that reports 4 to 7 hands back a value the vendor enumeration does
  // not define. Clamp everything above BATTERY to UNKNOWN rather than casting an
  // undefined value into the policy enumeration, whose verdict is an allowlist.
  const uint8_t reported = static_cast<uint8_t>(source);
  const PlatformPolicy::RailSupply supply =
      (known && (reported <= static_cast<uint8_t>(PlatformPolicy::RailSupply::BATTERY)))
          ? static_cast<PlatformPolicy::RailSupply>(reported)
          : PlatformPolicy::RailSupply::UNKNOWN;
  const PlatformPolicy::RailEnableVerdict verdict =
      PlatformPolicy::railEnableVerdict(known, supply);
  const PlatformPolicy::RailEnableVerdict previous = m_RailVerdict;
  m_RailVerdict = verdict;
  if (verdict != PlatformPolicy::RailEnableVerdict::ALLOW) {
    m_RailInterlockRefusals++;
    // WARN when the verdict changes, DEBUG for a repeat. Logging only the first
    // refusal of a boot hid a refusal that began minutes later, and the counter
    // is not the same thing: the console `rail status` reports both.
    if (verdict != previous) {
      ESP_LOGW(LOG_TAG, "EXT_5V output refused (%s), supply %s, refusals %lu",
               PlatformPolicy::railEnableVerdictName(verdict), known ? "known" : "unreadable",
               static_cast<unsigned long>(m_RailInterlockRefusals));
    } else {
      ESP_LOGD(LOG_TAG, "EXT_5V output refused (%s), refusals %lu",
               PlatformPolicy::railEnableVerdictName(verdict),
               static_cast<unsigned long>(m_RailInterlockRefusals));
    }
    return false;
  }

  return true;
}

bool Platform::readRailState(bool *on) {
  uint8_t config = 0;
  if (!readPowerConfig(&config)) {
    return false;
  }
  *on = (config & PWR_CFG_EXT_5V) != 0;
  m_RailOn = *on;
  return true;
}
#else
bool Platform::readRailState(bool *on) {
  *on = M5.Power.getExtOutput() != 0;
  m_RailOn = *on;
  return true;
}
#endif

uint32_t Platform::getBatteryFailCount(void) {
  return m_M5PM1FailCount;
}

Platform::battery_t Platform::readBattery(void) {
  battery_t battery = {0, 0, 0, false};

  if (m_BatteryCaps.level) {
    int32_t level = M5.Power.getBatteryLevel();
    battery.level = (level > 0) ? level : 0;
  }

#if FURBLE_HAS_M5PM1
  // the M5PM1 reports voltage and charge status, but no battery current
  if (m_BatteryCaps.voltage) {
    uint16_t mv = 0;
    if (m5pm1Access([this, &mv]() { return m_M5PM1.readVbat(&mv); })) {
      battery.voltage = mv;
    }
  }

  if (m_BatteryCaps.charging) {
    // charge status is on M5PM1 GPIO0, active low
    uint8_t charging = 1;
    if (m5pm1Access(
            [this, &charging]() { return m_M5PM1.gpioGetInput(M5PM1_GPIO_NUM_0, &charging); })) {
      battery.charging = (charging == 0);
    }
  }
#else
  if (m_BatteryCaps.voltage) {
    int32_t mv = M5.Power.getBatteryVoltage();
    battery.voltage = (mv > 0) ? mv : 0;
  }

  if (m_BatteryCaps.current) {
    battery.current = M5.Power.getBatteryCurrent();
  }

  if (m_BatteryCaps.charging) {
    battery.charging = (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging);
  }
#endif

  return battery;
}

Platform::battery_sample_t Platform::sampleBattery(void) {
  const std::lock_guard<std::mutex> guard(m_BatteryMutex);
  const auto &caps = m_BatteryCaps;
  const battery_t battery = readBattery();

  if (!m_BatterySampleInitialized) {
    m_BatterySample.meanLevel = battery.level;
    m_BatterySample.meanVoltage = battery.voltage;
    m_BatterySample.meanCurrent = battery.current;
    m_BatterySampleInitialized = true;
  } else {
    m_BatterySample.meanLevel += (battery.level - m_BatterySample.meanLevel) / 4.0f;
    if (caps.voltage) {
      m_BatterySample.meanVoltage += (battery.voltage - m_BatterySample.meanVoltage) / 4.0f;
    }
    if (caps.current) {
      m_BatterySample.meanCurrent += (battery.current - m_BatterySample.meanCurrent) / 12.0f;
    }
  }

  m_BatterySample.battery = battery;
  m_BatterySample.displayLevel = lroundf(m_BatterySample.meanLevel);
  return m_BatterySample;
}

Platform::battery_sample_t Platform::getBatterySample(void) const {
  const std::lock_guard<std::mutex> guard(m_BatteryMutex);
  return m_BatterySample;
}

Platform::battery_status_t Platform::getBatteryStatus(void) {
  battery_status_t raw = {};
  raw.level = M5.Power.getBatteryLevel();
  raw.voltage = M5.Power.getBatteryVoltage();
  raw.current = M5.Power.getBatteryCurrent();
  raw.charging = static_cast<int>(M5.Power.isCharging()) == 1;
  battery_status_t status = BatteryStatus::gate(getBatteryCaps(), raw);
#if FURBLE_HAS_VBUS_SENSE
  status.vbus = BatteryStatus::vbus(true, M5.Power.getVBUSVoltage());
#else
  // The board exposes no software-readable VBUS/PoE telemetry.
  status.vbus = BatteryStatus::vbus(false, 0);
#endif
  return status;
}

void Platform::update(void) {
  M5.update();
#if FURBLE_HAS_M5PM1
  // The UI calls update() every 5 ms, or up to every 20 ms with the panel
  // off. Reading the PMIC button on every 5 ms pass is about 200 I2C
  // transactions per second for a human-speed input; 50 Hz is plenty and cuts
  // the bus traffic tenfold.
  const uint32_t now = tick();
  if (PlatformPolicy::pm1PollDue(now, m_LastPm1PollMs)) {
    m_LastPm1PollMs = now;
    bool b = false;
    if (m_M5PM1.btnGetState(&b) == M5PM1_OK) {
      M5.BtnPWR.setRawState(tick(), b);
    }
  }
  watchdogFeed();
#else
  if (m_PMICHack && M5.BtnPWR.wasClicked()) {
    // fake PMIC button as actual button, record the click streak
    uint32_t now = tick();
    if (now - m_PMICClickTime < PWR_CLICK_THRESHOLD_MS) {
      m_PMICClickCount++;
    } else {
      m_PMICClickCount = 1;
    }
    m_PMICClickTime = now;
  }
#endif
}
}  // namespace Furble
