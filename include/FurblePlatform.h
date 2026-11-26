#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <array>
#include <mutex>

#include <esp_err.h>
#include <esp_log.h>

#include <M5PM1.h>

#include "FurbleBoardCaps.h"
#include "FurblePowerBackend.h"

namespace Furble {
#if defined(FURBLE_SIM)
namespace Sim {
const char *watchdogState(void);
const char *motionWakeState(void);
const char *pmicIrqState(void);
const char *pmicIrqStatus(void);
bool pmicInjectSystemIrq(uint8_t bits);
bool pmicClearSystemIrq(void);
}  // namespace Sim
#endif
/** Board power and platform services; meets the PowerBackend contract. */
class Platform: public PowerBackend {
 public:
  /** Selectable maximum CPU frequencies in MHz. */
  static constexpr std::array<uint8_t, 3> CPU_MAX_FREQ_MHZ = {80, 160, 240};

  /** Default maximum CPU frequency in MHz. */
  static constexpr uint8_t CPU_MAX_FREQ_DEFAULT_MHZ = 160;

  // battery_caps_t and battery_t come from PowerBackend.

  /**
   * Battery status for the companion status record.
   *
   * Fields hold the FurbleBatteryStatus.h values. Level, voltage, current and
   * charging are each gated by their own battery_caps_t flag. An unsupported
   * field is unknown: -1 for level and voltage, 0 for current and false for
   * charging. VBUS is read only where FURBLE_HAS_VBUS_SENSE is 1 and is 0
   * otherwise.
   */
  typedef struct {
    int32_t level;    // percent
    int16_t voltage;  // millivolts
    int32_t current;  // milliamps, positive when charging
    int16_t vbus;     // millivolts, 0 when not readable
    bool charging;
  } battery_status_t;

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
   * Refuses with false before any teardown in two cases. A row whose
   * `powerOffPath` is false has no way to remove power
   * (`PlatformPolicy::powerOffAction`); only the Waveshare ESP32-S3-ETH is in
   * that state, because with no PMIC the call it would otherwise reach arms no
   * wake source. Then, on every other board, an SD writer that did not
   * acknowledge the unmount refuses too, because cutting the rail with a track
   * open loses its tail (#494).
   *
   * Otherwise success never returns, and a PMIC refusal returns false with the
   * hardware watchdog left as the user set it.
   *
   * On the IP5306 Core there is no shutdown command, so the Off is the
   * vendor's deferred cut rather than an immediate one: M5Unified sleeps the
   * panel, clears the boost keep-on bit and deep sleeps, and the charger
   * removes the boost once the sleeping load has stayed under 45 mA for the
   * SYS_CTL2 window. On an AXP192, AXP2101 or M5PM1 board the rail drops
   * mid-call.
   */
  bool powerOff(void);

  /**
   * Arm the IMU interrupt as a light-sleep wake source.
   *
   * On the M5StickS3 both M5PM1 IRQ status registers are cleared first. The
   * PMIC holds its aggregated IRQ line, PYG1_IRQ on GPIO13, low until the IRQ
   * Status is cleared, and Status 2 carries 5VIN insert and remove, which
   * nothing in furble consumes. A latch left in either register therefore arms
   * this level-triggered wake source on a line that never releases. Clearing
   * rather than masking is deliberate: the clear is the release the vendor
   * manual documents, while what a mask does to the aggregated line is not.
   * See include/FurblePmicIrq.h.
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
   * Sample the IMU wake line and accumulate the assertion count.
   *
   * Called once per motion poll. On the M5StickS3 this reads and clears the
   * PMIC's latched GPIO4 interrupt flag, which is the only place the BMI270's
   * INT1 activity is visible without touching the wake pin, and then clears
   * IRQ Status 2 as well. Status 2 is not a motion signal and nothing here
   * consumes it, but a 5VIN insert or removal latches it and the PMIC holds
   * PYG1_IRQ low until it is cleared, so leaving it set would report an idle
   * IMU on a line that never goes quiet. On the StickC family the MPU6886
   * interrupt is latched in the sensor and holds the line, so the pin level is
   * read directly.
   *
   * Nothing here configures a GPIO interrupt. gpio_set_intr_type and
   * gpio_wakeup_enable write the same field and the IDF refuses an edge type on
   * a wakeup pin, so an edge counter on the wake pin cancels the wake source.
   *
   * @return true when the line was asserted at this sample.
   */
  bool motionWakeSample(void);

  /**
   * Assertions of the IMU wake line counted since it was armed.
   *
   * Gate step 1 reads this twice while the device is still and expects no
   * change. One increment per poll means the line is asserting continuously,
   * which on the M5StickS3 means a data interrupt is still mapped onto INT1 and
   * on the StickC family can also mean the RTC is driving the shared net.
   *
   * This counts polls at which the line was found asserted, not hardware edges:
   * a latched flag read at the poll rate cannot distinguish one assertion from
   * a thousand. It discriminates the failure gate step 1 is looking for, which
   * is a line that never goes quiet, and it does so without touching the wake
   * pin.
   */
  uint32_t motionWakeEdges(void) const;

  /** M5PM1 transactions that failed once and succeeded on the retry. */
  uint32_t getM5PM1RetryCount(void) const;

  /**
   * Clear a consumed IMU wake event at the PMIC.
   *
   * The M5StickS3 chains the IMU interrupt through M5PM1 GPIO4, which latches
   * its IRQ status. A latched status holds PYG1_IRQ asserted, and a level-
   * triggered wake source that never releases stops light sleep entirely, which
   * is the opposite of the point. Call this after consuming an event.
   *
   * IRQ Status 1 and Status 2 are both cleared. Only Status 1 carries motion,
   * but the aggregated line stays low while either latch is set, so a 5VIN
   * insert or removal during a wake would otherwise keep it asserted after the
   * motion event was consumed.
   */
  void clearMotionWake(void);

  /**
   * Largest early wake the BM8563 can produce for a valid request.
   *
   * The BM8563 has one-second resolution up to 255 seconds and minute
   * resolution above that, so a correct timer can fire up to 60 seconds before
   * the requested delay. The resume record is the shot deadline, which already
   * sits one wake margin later, so its early bound is the margin plus this.
   * The Plus2 and the Tough both use this timer path.
   */
  static constexpr uint32_t TIMED_WAKE_MAX_EARLY_S = 60;

  /**
   * Is a timed power-on supported by this board?
   */
  bool canTimedWake(void);

  /**
   * Power off and arrange for a timed power-on.
   *
   * On boards whose BM8563 INT drives the PMIC power key (the Tough), this
   * arms the timer, unmounts the SD card and cuts PMIC power; it does not
   * return on success. The Plus2 arms the same BM8563 timer and releases its
   * hold pin, returning true once the request is accepted.
   *
   * A refusal unwinds what it armed: an SD writer that never acknowledges
   * refuses the cut with the card mounted, the countdown cleared and, on the
   * M5PM1 path, the wake timer, the RTC RAM marker and the watchdog restored.
   *
   * @return true after the wake timer and power-off request were accepted;
   * false if setup failed or a handshake refused, and the device stays awake.
   */
  bool powerOffUntil(uint32_t seconds);

  /**
   * Consume the wake marker left by a timed power-on.
   */
  bool consumeTimedWake(void);

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
   * Does the board keep the GPS receiver's V_BCKP supplied when its main rail
   * is cut?
   *
   * A u-blox M10 keeps its battery-backed RAM and RTC on V_BCKP, so a backed
   * rail cut is a hot or warm start rather than a cold one, and the
   * save-on-shutdown backup is not needed (plan 321). No supported board wires
   * one; a board profile that does defines FURBLE_GPS_BACKUP_SUPPLY.
   */
  static bool hasGpsBackupSupply(void);

  /**
   * Can the board switch the external 5 V output off?
   *
   * The board profile's `gpsRail` column: the StickC, StickC Plus, StickS3,
   * Core2 and Tough switch the external 5 V output through their PMIC. On the
   * Core2 and Tough it is one BUS_5V net shared by Grove Port A and the M-Bus,
   * so Rail::GPS is the whole external 5 V net rather than a private GPS rail.
   * On the Core (IP5306), the StickC Plus2 and the Waveshare board the rail is
   * always on, and the headless profile has no rail control. The switch request
   * is advisory: M5Unified refuses an enable on low battery with VBUS current
   * and no ACIN, which Platform::setRail() reports through its readback. A
   * receiver on a rail that cannot be cut must never be stopped for a
   * save-on-shutdown, because nothing would ever restart it. Valid after
   * Platform::init().
   */
  static bool canCutGpsRail(void);

  /**
   * Can the panel be turned off without the PMIC removing power?
   *
   * True on every board except an IP5306 board whose boost keep-on write did
   * not read back at boot. That write is the only thing that keeps a panel-off
   * load above the PMIC's 45 mA light-load threshold, so the UI must fall back
   * to Dim when it failed.
   */
  static bool canTurnDisplayOff(void);

  /**
   * Get the battery measurements supported by this board.
   *
   * Returned by value: the simulator refreshes the flags from the hardware
   * model on every call, so a reference to shared mutable storage would race
   * console and UI readers.
   */
  battery_caps_t getBatteryCaps(void);

  /**
   * Read the battery, unsupported measurements are returned as zero.
   */
  battery_t readBattery(void);

  /** Read the battery and update the shared exponentially weighted averages. */
  battery_sample_t sampleBattery(void);

  /** Get the most recent battery sample without touching the hardware. */
  battery_sample_t getBatterySample(void) const;

  /**
   * Read the battery status reported to the companion app.
   *
   * A board with a battery backend reports the raw M5Unified readings. A
   * board without one, the headless profiles, reports every field as
   * unknown, so a fallback reading is never claimed as a battery. VBUS is
   * reported where the board can sense it.
   */
  battery_status_t getBatteryStatus(void);

  /**
   * Get the battery capacity in mAh, zero if unknown.
   */
  uint16_t getBatteryCapacity(void);

  /**
   * Switch a named rail. The M5 boards switch only the GPS rail, which is the
   * Port A 5 V output.
   *
   * @return true when the rail reads back in the requested state; false when
   *         the board has no switch or the PMIC did not take the write.
   */
  bool setRail(Rail rail, bool on);

  /**
   * Get the count of PMIC accesses which failed after a retry.
   */
  uint32_t getBatteryFailCount(void);

#if FURBLE_HAS_M5PM1
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
  // Simulator observability for the retained PMIC model: the watchdog state
  // reader, the aggregated IRQ line, the motion wake gate and the two injected
  // PMIC events. Firmware has no callers for any of them.
  friend const char *Sim::watchdogState(void);
  friend const char *Sim::motionWakeState(void);
  friend const char *Sim::pmicIrqState(void);
  friend const char *Sim::pmicIrqStatus(void);
  friend bool Sim::pmicInjectSystemIrq(uint8_t bits);
  friend bool Sim::pmicClearSystemIrq(void);
#endif
  Platform() {};

  /**
   * Is the frequency one we are prepared to ask for?
   */
  static bool isCPUMaxFreqValid(uint8_t mhz);

#if !FURBLE_HAS_M5PM1
  // Power button click streak threshold. The streak exists only for boards
  // whose PMIC button level is not readable; the M5PM1 image reads the button
  // directly, so the mechanism is compiled out there and clang's
  // unused-private-field check keeps it that way.
  const uint8_t PWR_CLICK_THRESHOLD_MS = 20;
#endif

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

  /**
   * Hold the IP5306 boost on for the session.
   *
   * No-op unless the detected PMIC is an IP5306. M5Unified clears SYS_CTL0
   * bit 1 at M5.begin(), which lets the PMIC cut the 5 V boost after the
   * light-load window whenever the load stays below 45 mA, as a panel-off Core
   * does. Logs and leaves the bit clear if the write or readback fails.
   */
  void keepIp5306BoostOn(void);

  /** True until an IP5306 keep-on write fails to read back. */
  static bool m_Ip5306BoostHeld;

#if FURBLE_HAS_M5PM1
  /** Ensure the PMIC long-press download recovery path is not locked. */
  bool unlockDownloadRecovery(void);

  /**
   * Clear both M5PM1 IRQ status latches, retried per register.
   *
   * The two writes release PYG1_IRQ: the vendor manual holds a GPIO configured
   * as IRQ low until the IRQ Status is cleared, and nothing in furble consumes
   * a Status 2 bit. Register order and the mask-versus-clear decision live in
   * include/FurblePmicIrq.h.
   *
   * @return true when both writes were accepted.
   */
  bool clearPmicIrqLatches(void);
#endif

  M5PM1 m_M5PM1;

  bool m_Init = false;
  bool m_PMICHack = false;
  uint8_t m_CPUMaxFreqMHz = CPU_MAX_FREQ_DEFAULT_MHZ;
  uint32_t m_LastPm1PollMs = 0;
  uint8_t m_PMICClickCount = 0;
#if !FURBLE_HAS_M5PM1
  uint32_t m_PMICClickTime = 0;
#endif

  battery_caps_t m_BatteryCaps = {false, false, false, false};
  uint16_t m_BatteryCapacity = 0;
  uint32_t m_M5PM1RetryCount = 0;
  uint32_t m_M5PM1FailCount = 0;
  mutable std::mutex m_BatteryMutex;
  battery_sample_t m_BatterySample = {};
  bool m_BatterySampleInitialized = false;
  bool m_MotionWakeArmed = false;
#if FURBLE_HAS_M5PM1
  bool m_StatusLedLevel = false;
  bool m_StatusLedLevelValid = false;
  bool m_WatchdogEnabled = false;
  uint32_t m_WatchdogLastFeed = 0;
#endif
};

static_assert(meetsPowerBackend<Platform>(),
              "Platform must meet the PowerBackend contract without virtual dispatch");
}  // namespace Furble

#endif
