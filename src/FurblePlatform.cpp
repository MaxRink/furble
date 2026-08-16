#include <algorithm>

#include <esp_pm.h>

#include <M5PM1.h>
#include <M5Unified.h>

#include "FurblePlatform.h"
#include "FurbleTypes.h"

namespace Furble {

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    instance.setSleep(true);

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_spk = false;
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
    instance.m_M5PM1.setDownloadLock(true);        // disable BtnPWR long-press enter download mode
#endif

    instance.initBattery();

    instance.m_Init = true;
  }

  return instance;
}

void Platform::init(void) {
  (void)getInstance();
}

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

esp_err_t Platform::configurePM(uint8_t max_freq_mhz, bool sleep) {
  esp_pm_config_t pm_config = {
      .max_freq_mhz = max_freq_mhz,
      .min_freq_mhz = CPU_MIN_FREQ_MHZ,
      .light_sleep_enable = sleep,
  };
  return esp_pm_configure(&pm_config);
}

bool Platform::isCPUMaxFreqValid(uint8_t mhz) {
  return std::find(CPU_MAX_FREQ_MHZ.begin(), CPU_MAX_FREQ_MHZ.end(), mhz) != CPU_MAX_FREQ_MHZ.end();
}

void Platform::setSleep(bool enable) {
  m_Sleep = enable;
  ESP_ERROR_CHECK(configurePM(m_CPUMaxFreqMHz, enable));
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
  esp_err_t err = configurePM(freq, m_Sleep);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "CPU maximum frequency %dMHz rejected (%s), using %dMHz.", freq,
             esp_err_to_name(err), CPU_MAX_FREQ_DEFAULT_MHZ);
    freq = CPU_MAX_FREQ_DEFAULT_MHZ;
    ESP_ERROR_CHECK(configurePM(freq, m_Sleep));
  }

  m_CPUMaxFreqMHz = freq;

  ESP_LOGI(LOG_TAG, "CPU maximum frequency %dMHz.", m_CPUMaxFreqMHz);
}

uint8_t Platform::getCPUMaxFreq(void) const {
  return m_CPUMaxFreqMHz;
}

void Platform::powerOff(void) {
#if defined(FURBLE_M5STICKS3)
  m_M5PM1.shutdown();
#else
  M5.Power.powerOff();
#endif
}

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
      m_BatteryCaps = {true, false, false, false};
      m_BatteryCapacity = 0;
  }

  ESP_LOGI("platform", "Battery: level=%u voltage=%u current=%u charging=%u capacity=%umAh",
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

void Platform::update(void) {
  M5.update();
#if defined(FURBLE_M5STICKS3)
  bool b = false;
  if (m_M5PM1.btnGetState(&b) == M5PM1_OK) {
    M5.BtnPWR.setRawState(tick(), b);
  }
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
