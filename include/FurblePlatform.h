#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <array>
#include <mutex>

#include <esp_err.h>
#include <esp_log.h>

#include <M5PM1.h>

namespace Furble {
#if defined(FURBLE_SIM)
namespace Sim {
const char *watchdogState(void);
}
#endif
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

  /** Battery sample with the shared UI and console EWMA values. */
  typedef struct {
    battery_t battery;
    float meanLevel;
    float meanVoltage;
    float meanCurrent;
    uint8_t displayLevel;
  } battery_sample_t;

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

  /** Set and verify the StickS3 PMIC watchdog state. */
  bool watchdogEnable(bool enable);

  /** Disconnect cameras and disable restart-sensitive hardware before reset. */
  void prepareRestart(void);

  /** Gracefully prepare the device and restart it. */
  void restart(void);

  /**
   * Feed the M5PM1 hardware watchdog.
   */
  void watchdogFeed(void);

  /**
   * Wake the M5PM1 with a harmless retried read.
   *
   * The first I2C transaction after its idle sleep fails and only wakes it.
   * Call this before an M5PM1 access whose I2C status is not surfaced, such
   * as the M5Unified speaker amplifier enable.
   */
  void wakeM5PM1(void);

  /**
   * Apply the display-off status LED policy for boards with PMIC hardware.
   */
  void setDisplayOff(bool off);

  /**
   * Power off the device.
   *
   * On success this call never returns. Returns false if the PMIC refused the
   * shutdown, with the hardware watchdog re-armed if it was armed before.
   */
  bool powerOff(void);

  /**
   * Arm the IMU interrupt as a light-sleep wake source.
   *
   * @return true when this board has a configured interrupt path.
   */
  bool armMotionWake(void);

  /**
   * Disarm the IMU light-sleep wake source.
   */
  void disarmMotionWake(void);

  /**
   * True while the IMU interrupt line is asserted.
   *
   * Both engines drive the line active low, so this reports a low level. The
   * MPU6886 status register is clear-on-read and M5Unified's IMU update()
   * reads it, so the pin is the only motion signal this project owns outright.
   *
   * @return false when no wake path is armed on this board.
   */
  bool motionWakeAsserted(void) const;

  /**
   * Clear a consumed IMU wake event at the PMIC.
   *
   * The M5StickS3 chains the IMU interrupt through M5PM1 GPIO4, which latches
   * its IRQ status. A latched status holds PYG1_IRQ asserted, and a level-
   * triggered wake source that never releases stops light sleep entirely, which
   * is the opposite of the point. Call this after consuming an event.
   */
  void clearMotionWake(void);

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

  /** Read the battery and update the shared exponentially weighted averages. */
  battery_sample_t sampleBattery(void);

  /** Get the most recent battery sample without touching the hardware. */
  battery_sample_t getBatterySample(void) const;

  /**
   * Get the battery capacity in mAh, zero if unknown.
   */
  uint16_t getBatteryCapacity(void);

  /**
   * Get the count of PMIC accesses which failed after a retry.
   */
  uint32_t getBatteryFailCount(void);

#if defined(FURBLE_M5STICKS3)
  /**
   * Prepare the StickS3 for an intentional serial flash.
   *
   * This is only exposed by the developer console. It disables the PMIC
   * watchdog and verifies that the PMIC will still accept its long-press
   * download recovery gesture.
   */
  bool prepareFlash(void);

  /** Read back whether the PMIC long-press recovery path is unlocked. */
  bool downloadRecoveryUnlocked(void);

  /**
   * Undo prepareFlash after an upload was cancelled.
   */
  bool cancelFlashPreparation(void);
#endif

 private:
#if defined(FURBLE_SIM)
  friend const char *Sim::watchdogState(void);
#endif
  Platform() {};

  /**
   * Is the frequency one we are prepared to ask for?
   */
  static bool isCPUMaxFreqValid(uint8_t mhz);

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

#if defined(FURBLE_M5STICKS3)
  /** Ensure the PMIC long-press download recovery path is not locked. */
  bool unlockDownloadRecovery(void);
#endif

  M5PM1 m_M5PM1;

  bool m_Init = false;
  bool m_PMICHack = false;
  uint8_t m_CPUMaxFreqMHz = CPU_MAX_FREQ_DEFAULT_MHZ;
  uint8_t m_PMICClickCount = 0;
  uint32_t m_PMICClickTime = 0;

  battery_caps_t m_BatteryCaps = {false, false, false, false};
  uint16_t m_BatteryCapacity = 0;
  uint32_t m_M5PM1RetryCount = 0;
  uint32_t m_M5PM1FailCount = 0;
  mutable std::mutex m_BatteryMutex;
  battery_sample_t m_BatterySample = {};
  bool m_BatterySampleInitialized = false;
  bool m_MotionWakeArmed = false;
#if defined(FURBLE_M5STICKS3)
  bool m_StatusLedLevel = false;
  bool m_StatusLedLevelValid = false;
  bool m_WatchdogEnabled = false;
  uint32_t m_WatchdogLastFeed = 0;
#endif
};
}  // namespace Furble

#endif
