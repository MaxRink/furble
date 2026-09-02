// Host platform shim for the console command suite.
//
// src/FurblePlatform.cpp is hardware bound (M5PM1 I2C, esp_pm, the PMIC
// watchdog) and is not built here, so the double models the surface the
// console and Control touch. The battery, power management and PMIC flash
// preparation state are all test settable, which is what lets the 'status',
// 'power log' and 'flash' commands run their real code paths.
//
// The include guard matches the real header exactly, so if the real header is
// reached through a transitive include it is a no-op here.
#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <array>
#include <cstdint>

// ble_gap_conn_cancel() cancels an in-flight NimBLE connection attempt. On the
// device a transitively included NimBLE host header declares it. The mock does
// not, so the shim declares it here, as the control host shim does.
extern "C" int ble_gap_conn_cancel(void);

namespace Furble {

class Platform {
 public:
  static constexpr std::array<uint8_t, 3> CPU_MAX_FREQ_MHZ = {80, 160, 240};
  static constexpr uint8_t CPU_MAX_FREQ_DEFAULT_MHZ = 160;

  typedef struct {
    bool level;
    bool voltage;
    bool current;
    bool charging;
  } battery_caps_t;

  typedef struct {
    uint8_t level;
    uint16_t voltage;
    int32_t current;
    bool charging;
  } battery_t;

  typedef struct {
    battery_t battery;
    float meanLevel;
    float meanVoltage;
    float meanCurrent;
    uint8_t displayLevel;
  } battery_sample_t;

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

  void watchdogFeed(void) {}
  void restart(void);
  void dumpPMLocks(void);

  const battery_caps_t &getBatteryCaps(void) const { return m_Caps; }
  battery_t readBattery(void) const { return m_Sample.battery; }
  battery_sample_t getBatterySample(void) const { return m_Sample; }
  pm_config_t getPMConfig(void) const { return m_PM; }
  uint16_t getBatteryCapacity(void) const { return m_Capacity; }

  /** Disarm the PMIC watchdog so a serial upload cannot be interrupted. */
  bool prepareFlash(void);

  /** Re-arm the PMIC watchdog after an abandoned upload. */
  bool cancelFlashPreparation(void);

  // Test surface. The PMIC is a separate chip, so its armed state and its
  // refusals are modelled as retained, injectable state.
  void setBatteryCaps(const battery_caps_t &caps) { m_Caps = caps; }
  void setBatterySample(const battery_sample_t &sample) { m_Sample = sample; }
  void setBatteryCapacity(uint16_t capacity) { m_Capacity = capacity; }
  void setPMConfig(const pm_config_t &config) { m_PM = config; }
  void setFlashPrepareShouldFail(bool fail) { m_PrepareFails = fail; }
  void setFlashCancelShouldFail(bool fail) { m_CancelFails = fail; }
  bool isFlashReady(void) const { return m_FlashReady; }
  uint32_t restartCount(void) const { return m_Restarts; }
  uint32_t dumpPMLocksCount(void) const { return m_DumpPMLocks; }

 private:
  Platform() {}

  battery_caps_t m_Caps = {true, true, true, true};
  battery_sample_t m_Sample = {
      {72, 3900, -110, false},
      72.0f,
      3900.0f,
      -110.0f,
      72
  };
  pm_config_t m_PM = {160, 40, true};
  uint16_t m_Capacity = 200;
  bool m_PrepareFails = false;
  bool m_CancelFails = false;
  bool m_FlashReady = false;
  uint32_t m_Restarts = 0;
  uint32_t m_DumpPMLocks = 0;
};

}  // namespace Furble

#endif
