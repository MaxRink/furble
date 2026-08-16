#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <array>

#include <esp_err.h>
#include <esp_log.h>

#include <M5PM1.h>

namespace Furble {
class Platform {
 public:
  /** Selectable maximum CPU frequencies in MHz. */
  static constexpr std::array<uint8_t, 3> CPU_MAX_FREQ_MHZ = {80, 160, 240};

  /** Default maximum CPU frequency in MHz. */
  static constexpr uint8_t CPU_MAX_FREQ_DEFAULT_MHZ = 160;

  /**
   * Battery measurements available on this board.
   */
  typedef struct {
    bool level;
    bool voltage;
    bool current;
    bool charging;
  } battery_caps_t;

  /**
   * Battery measurement sample.
   *
   * Only fields flagged in battery_caps_t are meaningful.
   */
  typedef struct {
    uint8_t level;     // percent
    uint16_t voltage;  // millivolts
    int32_t current;   // milliamps, positive when charging
    bool charging;
  } battery_t;

  /**
   * Power management configuration.
   *
   * The frequencies are the configured ceiling and floor, not a measurement of
   * what the CPU is running at right now.
   */
  typedef struct {
    uint8_t max_freq_mhz;
    uint8_t min_freq_mhz;
    bool light_sleep_enable;
  } pm_config_t;

  static Platform &getInstance();

  Platform(Platform const &) = delete;
  Platform(Platform &&) = delete;
  Platform &operator=(Platform const &) = delete;
  Platform &operator=(Platform &&) = delete;

  static void init(void);

  /**
   * Get time since boot in milliseconds.
   */
  uint32_t tick(void);

  /**
   * Get debounced power button click count.
   */
  uint8_t getPWRClickCount(void);

  /**
   * Update platform specific state.
   */
  void update(void);

  /**
   * Power off the device.
   */
  void powerOff(void);

  /**
   * Enable or disable automatic and light sleep.
   */
  void setSleep(bool enable);

  /**
   * Set the maximum CPU frequency in MHz.
   *
   * Unsupported values fall back to the default. Use getCPUMaxFreq() to read
   * back what was actually applied.
   */
  void setCPUMaxFreq(uint8_t mhz);

  /**
   * Get the maximum CPU frequency in MHz.
   */
  uint8_t getCPUMaxFreq(void) const;

  /**
   * Read back the live power management configuration.
   */
  pm_config_t getPMConfig(void);

  /**
   * Dump the power management locks to the console.
   *
   * The output goes to the serial console, not the display. Hold times are only
   * reported with CONFIG_PM_PROFILING, which is off in the shipping build.
   */
  void dumpPMLocks(void);

  /**
   * Is tickless idle compiled in?
   */
  static bool hasTicklessIdle(void);

  /**
   * Get the battery measurements supported by this board.
   */
  const battery_caps_t &getBatteryCaps(void);

  /**
   * Read the battery, unsupported measurements are returned as zero.
   */
  battery_t readBattery(void);

  /**
   * Get the battery capacity in mAh, zero if unknown.
   */
  uint16_t getBatteryCapacity(void);

  /**
   * Get the count of PMIC accesses which failed after a retry.
   */
  uint32_t getBatteryFailCount(void);

 private:
  Platform() {};

  /**
   * Apply the power management configuration.
   */
  esp_err_t configurePM(uint8_t max_freq_mhz, bool sleep);

  /**
   * Is the frequency one we are prepared to ask for?
   */
  static bool isCPUMaxFreqValid(uint8_t mhz);

  const int CPU_MIN_FREQ_MHZ = 40;

  // Power button click streak threshold
  const uint8_t PWR_CLICK_THRESHOLD_MS = 20;

  /**
   * Perform an M5PM1 access, retrying once on failure.
   *
   * The M5PM1 sleeps after an I2C idle period, the vendor documents that the
   * first access after that only wakes the device and fails.
   */
  template <typename T>
  bool m5pm1Access(T &&access) {
    if (access() == M5PM1_OK) {
      return true;
    }

    m_M5PM1RetryCount++;
    ESP_LOGD("platform", "M5PM1 access failed, retrying (%lu)", m_M5PM1RetryCount);

    if (access() == M5PM1_OK) {
      return true;
    }

    m_M5PM1FailCount++;
    ESP_LOGW("platform", "M5PM1 access failed after retry (%lu)", m_M5PM1FailCount);

    return false;
  }

  /**
   * Record which battery measurements this board supports.
   */
  void initBattery(void);

  M5PM1 m_M5PM1;

  bool m_Init = false;
  bool m_PMICHack = false;
  bool m_Sleep = true;
  uint8_t m_CPUMaxFreqMHz = CPU_MAX_FREQ_DEFAULT_MHZ;
  uint8_t m_PMICClickCount = 0;
  uint32_t m_PMICClickTime = 0;

  battery_caps_t m_BatteryCaps = {false, false, false, false};
  uint16_t m_BatteryCapacity = 0;
  uint32_t m_M5PM1RetryCount = 0;
  uint32_t m_M5PM1FailCount = 0;
};
}  // namespace Furble

#endif
