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
