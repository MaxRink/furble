#include <algorithm>
#include <cmath>

#include <esp_system.h>

#include <M5PM1.h>
#include <M5Unified.h>

#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleWatchdog.h"

namespace Furble {

#if defined(FURBLE_M5STICKS3)
namespace {
using Watchdog::PM1_FEED_PERIOD_MS;
using Watchdog::PM1_TIMEOUT_S;
}  // namespace
#endif

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    Power::init();
    instance.setCPUMaxFreq(CPU_MAX_FREQ_DEFAULT_MHZ);

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_spk = Feedback::outputIncludesSound(
        static_cast<Feedback::output_t>(Settings::load<uint8_t>(Settings::FB_OUTPUT)));
    cfg.internal_mic = false;
    cfg.pmic_button = true;
    M5.begin(cfg);

    switch (M5.getBoard()) {
      case m5::board_t::board_M5StickC:
      case m5::board_t::board_M5StickCPlus:
      case m5::board_t::board_M5Tough:
        instance.m_PMICHack = true;
        break;
      default:
        instance.m_PMICHack = false;
    }

#if defined(FURBLE_M5STICKS3)
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

    instance.m_Init = true;
  }

  return instance;
}

void Platform::init(void) {
  (void)getInstance();
}

void Platform::prepareRestart(void) {
  auto &control = Control::getInstance();
  // forRestart == true: esp_restart() runs immediately after, so a force-
  // complete on timeout is safe here. The reset kills any in-flight BLE
  // teardown, so nothing can reconnect and race the still-freeing client.
  if (!control.disconnect(Control::DISCONNECT_TIMEOUT_MS, true)) {
    ESP_LOGW(LOG_TAG, "Restart continuing after camera disconnect timeout.");
  }

#if defined(FURBLE_M5STICKS3)
  watchdogEnable(false);
#endif
}

void Platform::restart(void) {
  prepareRestart();
  esp_restart();
}

#if defined(FURBLE_M5STICKS3)
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

bool Platform::prepareFlash(void) {
  // A serial upload can spend longer than the normal 10 second PMIC window
  // in ROM download mode. The caller must be a deliberate local console user;
  // the regular runtime watchdog is restored on the next application boot.
  watchdogEnable(false);
  uint8_t watchdogCount = 1;
  if (!m5pm1Access(
          [this, &watchdogCount]() { return m_M5PM1.wdtGetCount(&watchdogCount); })
      || watchdogCount != 0) {
    ESP_LOGE(LOG_TAG, "M5PM1 watchdog did not verify disabled for flash");
    return false;
  }

  if (!unlockDownloadRecovery()) {
    watchdogEnable(true);
    return false;
  }

  ESP_LOGW(LOG_TAG,
           "M5PM1 watchdog disabled for flash; upload now, or run 'flash cancel'");
  return true;
}

bool Platform::downloadRecoveryUnlocked(void) {
  bool locked = true;
  if (!m5pm1Access([this, &locked]() { return m_M5PM1.getDownloadLock(&locked); })) {
    return false;
  }
  return !locked;
}

void Platform::cancelFlashPreparation(void) {
  watchdogEnable(true);
  ESP_LOGI(LOG_TAG, "M5PM1 watchdog restored after cancelled flash");
}

void Platform::watchdogEnable(bool enable) {
  m_WatchdogEnabled = false;
  m_WatchdogLastFeed = tick();

  const uint8_t timeout = enable ? PM1_TIMEOUT_S : 0;
  if (!m5pm1Access([this, timeout]() { return m_M5PM1.wdtSet(timeout); })) {
    ESP_LOGE(LOG_TAG, "Failed to set M5PM1 watchdog to %u seconds", static_cast<unsigned>(timeout));
    return;
  }

  if (enable) {
    m_WatchdogEnabled = true;
    ESP_LOGI(LOG_TAG, "M5PM1 watchdog armed for %u seconds", static_cast<unsigned>(PM1_TIMEOUT_S));
  } else {
    ESP_LOGI(LOG_TAG, "M5PM1 watchdog disabled");
  }
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
  if (off) {
    if (m_StatusLedLevelValid) {
      return;
    }

    uint8_t powerConfig = 0;
    if (!m5pm1Access([this, &powerConfig]() { return m_M5PM1.getPowerConfig(&powerConfig); })) {
      ESP_LOGW(LOG_TAG, "Unable to read StickS3 status LED level");
      return;
    }

    m_StatusLedLevel = (powerConfig & M5PM1_PWR_CFG_LED_CTRL) != 0;
    if (!m5pm1Access([this]() { return m_M5PM1.setLedEnLevel(false); })) {
      ESP_LOGW(LOG_TAG, "Unable to turn off StickS3 status LED");
      return;
    }

    m_StatusLedLevelValid = true;
    return;
  }

  if (!m_StatusLedLevelValid) {
    return;
  }

  const bool ledLevel = m_StatusLedLevel;
  if (!m5pm1Access([this, ledLevel]() { return m_M5PM1.setLedEnLevel(ledLevel); })) {
    ESP_LOGW(LOG_TAG, "Unable to restore StickS3 status LED level");
    return;
  }

  m_StatusLedLevelValid = false;
}
#endif

#if !defined(FURBLE_M5STICKS3)
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
    ESP_LOGW(LOG_TAG, "CPU maximum frequency %dMHz rejected (%s), using %dMHz.", freq,
             esp_err_to_name(err), CPU_MAX_FREQ_DEFAULT_MHZ);
    freq = CPU_MAX_FREQ_DEFAULT_MHZ;
    ESP_ERROR_CHECK(power.configure(freq));
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
    ESP_LOGW(LOG_TAG, "Unable to read power management configuration (%s).", esp_err_to_name(err));
    return {m_CPUMaxFreqMHz, Power::CPU_MIN_FREQ_MHZ, true};
  }

  return {static_cast<uint8_t>(pm_config.max_freq_mhz),
          static_cast<uint8_t>(pm_config.min_freq_mhz), pm_config.light_sleep_enable};
}

void Platform::dumpPMLocks(void) {
  esp_err_t err = esp_pm_dump_locks(stdout);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "Unable to dump power management locks (%s).", esp_err_to_name(err));
  }
}

bool Platform::hasTicklessIdle(void) {
#if defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
  return true;
#else
  return false;
#endif
}

bool Platform::powerOff(void) {
  // the SD writer task closes the track and unmounts, wait for it to finish
  SD::getInstance().powerOff();

#if defined(FURBLE_M5STICKS3)
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
  M5.Power.powerOff();
  return true;
#endif
}

#if !defined(FURBLE_M5STICKS3)
void Platform::setDisplayOff(bool off) {
  (void)off;
}
#endif

void Platform::initBattery(void) {
  // capabilities follow the PMIC, capacities are from the vendor product pages
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
      // AXP192
      m_BatteryCaps = {true, true, true, true};
      m_BatteryCapacity = 95;
      break;

    case m5::board_t::board_M5StickCPlus:
      // AXP192
      m_BatteryCaps = {true, true, true, true};
      m_BatteryCapacity = 120;
      break;

    case m5::board_t::board_M5StackCore2:
    case m5::board_t::board_M5Tough:
      // AXP192
      m_BatteryCaps = {true, true, true, true};
      m_BatteryCapacity = 390;
      break;

    case m5::board_t::board_M5StickS3:
      // M5PM1, which has no battery current backend in M5Unified and no
      // documented battery current register
      m_BatteryCaps = {true, true, false, true};
      m_BatteryCapacity = 250;
      break;

    case m5::board_t::board_M5StickCPlus2:
      // no PMIC, battery voltage is read from an ADC divider
      m_BatteryCaps = {true, true, false, false};
      m_BatteryCapacity = 200;
      break;

    case m5::board_t::board_M5Stack:
      // IP5306, coarse level only
      m_BatteryCaps = {true, false, false, false};
      m_BatteryCapacity = 0;
      break;

    default:
      // An unrecognized board has no trustworthy battery backend. In
      // particular, the Waveshare ESP32-S3-ETH is a mains/PoE node with no
      // battery or PMIC. Do not turn M5Unified's fallback values into a fake
      // battery percentage.
      m_BatteryCaps = {false, false, false, false};
      m_BatteryCapacity = 0;
  }

  ESP_LOGI(LOG_TAG, "Battery: level=%u voltage=%u current=%u charging=%u capacity=%umAh",
           m_BatteryCaps.level, m_BatteryCaps.voltage, m_BatteryCaps.current,
           m_BatteryCaps.charging, m_BatteryCapacity);
}

const Platform::battery_caps_t &Platform::getBatteryCaps(void) {
  return m_BatteryCaps;
}

uint16_t Platform::getBatteryCapacity(void) {
  return m_BatteryCapacity;
}

uint32_t Platform::getBatteryFailCount(void) {
  return m_M5PM1FailCount;
}

Platform::battery_t Platform::readBattery(void) {
  battery_t battery = {0, 0, 0, false};

  if (m_BatteryCaps.level) {
    int32_t level = M5.Power.getBatteryLevel();
    battery.level = (level > 0) ? level : 0;
  }

#if defined(FURBLE_M5STICKS3)
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

void Platform::update(void) {
  M5.update();
#if defined(FURBLE_M5STICKS3)
  bool b = false;
  if (m_M5PM1.btnGetState(&b) == M5PM1_OK) {
    M5.BtnPWR.setRawState(tick(), b);
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
