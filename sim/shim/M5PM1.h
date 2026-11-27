#ifndef FURBLE_SIM_M5PM1_H
#define FURBLE_SIM_M5PM1_H

#include <algorithm>
#include <cstdint>
#include <mutex>

#include "clock.h"

// Host model of the M5Stack PM1 driver the StickS3 image links.
//
// Symbol names, register layout and edge cases follow the vendored driver,
// M5PM1 v1.0.6 (platformio.ini, lib_deps `M5PM1@1.0.6`) src/M5PM1.h and
// src/M5PM1.cpp. Every constant below carries the vendor register and bit it
// comes from, so a driver update can be checked against this file. The host has
// no I2C bus, so each transaction only moves this model forward.
inline constexpr int M5PM1_OK = 0;
inline constexpr int M5PM1_ERROR = -1;

// GPIO pin numbers, M5PM1.h m5pm1_gpio_num_t. GPIO0 carries the charge status
// input, GPIO4 the IMU interrupt on the StickS3.
inline constexpr uint8_t M5PM1_GPIO_NUM_0 = 0;
inline constexpr uint8_t M5PM1_GPIO_NUM_4 = 4;
// M5PM1.h m5pm1_gpio_mode_t: 0 = input, 1 = output (REG_GPIO_MODE 0x10).
inline constexpr uint8_t M5PM1_GPIO_MODE_INPUT = 0x00;
// M5PM1.h m5pm1_gpio_func_t: 01 = IRQ (REG_GPIO_FUNC1 0x17 for GPIO4).
inline constexpr uint8_t M5PM1_GPIO_FUNC_IRQ = 0x01;
// M5PM1.h m5pm1_gpio_pull_t: 01 = pull-up (REG_GPIO_PUPD 0x14 and 0x15).
inline constexpr uint8_t M5PM1_GPIO_PULL_UP = 0x01;
// M5PM1.h m5pm1_gpio_drive_t: 0 = push-pull, 1 = open-drain (REG_GPIO_DRV 0x13).
inline constexpr uint8_t M5PM1_GPIO_DRIVE_PUSHPULL = 0x00;
// M5PM1.h m5pm1_gpio_wake_edge_t: 0 = falling, 1 = rising (REG_GPIO_WAKE_CFG 0x19).
inline constexpr uint8_t M5PM1_GPIO_WAKE_FALLING = 0x00;
// M5PM1.h m5pm1_irq_gpio_t: one bit per pin in REG_IRQ_STATUS1 0x40.
inline constexpr uint8_t M5PM1_IRQ_GPIO4 = 1u << 4;
inline constexpr uint8_t M5PM1_IRQ_GPIO_ALL = 0x1F;
// M5PM1.h m5pm1_irq_mask_ctrl_t: 0 = unmask (interrupt allowed), 1 = mask.
// The names read backwards from the bit, which is the vendor's naming.
inline constexpr uint8_t M5PM1_IRQ_MASK_DISABLE = 0x00;
inline constexpr uint8_t M5PM1_IRQ_MASK_ENABLE = 0x01;
// M5PM1.h m5pm1_clean_type_t for the status readers.
inline constexpr uint8_t M5PM1_CLEAN_NONE = 0x00;
inline constexpr uint8_t M5PM1_CLEAN_ONCE = 0x01;
inline constexpr uint8_t M5PM1_CLEAN_ALL = 0x02;
// M5PM1.h m5pm1_wake_src_t: REG_WAKE_SRC 0x05 bit 0 is the timer wake.
inline constexpr uint8_t M5PM1_WAKE_SRC_TIM = 0x01;
// M5PM1.h REG_PWR_CFG 0x06 bit 4 enables the status LED level. Register 0x00
// is DEVICE_ID, not PWR_CFG: the manual's register list and M5Unified 0.2.20
// M5PM1_Class.cpp:15 and :18 give 0x00 as the identity and 0x06 as the power
// configuration, and docs/components/m5pm1.md records both.
inline constexpr uint8_t M5PM1_PWR_CFG_LED_CTRL = (1 << 4);
// M5PM1.h REG_PWR_CFG 0x06 bit 3 is BOOST_EN, the BOOST/GROVE 5 V output that
// is the StickS3 EXT_5V port.
inline constexpr uint8_t M5PM1_PWR_CFG_BOOST_EN = 0x08;

// M5PM1.h m5pm1_pwr_src_t: REG_PWR_SRC 0x04 bits 2:0. Values 4 to 7 are not
// named, which is the point: the production clamp maps them to UNKNOWN.
enum m5pm1_pwr_src_t {
  M5PM1_PWR_SRC_5VIN = 0,
  M5PM1_PWR_SRC_5VINOUT = 1,
  M5PM1_PWR_SRC_BAT = 2,
  M5PM1_PWR_SRC_UNKNOWN = 3,
};
// M5PM1.h m5pm1_tim_action_t: REG_TIM_CFG 0x3C [2:0], 011 = power on.
inline constexpr uint8_t M5PM1_TIM_ACTION_POWERON = 0x03;
// M5PM1.h REG_IRQ_STATUS2 0x41 system flags. The battery events need charging
// enabled and the 5VINOUT events need input mode, per the vendor comment.
inline constexpr uint8_t M5PM1_IRQ_SYS_5VIN_IN = 1u << 0;
inline constexpr uint8_t M5PM1_IRQ_SYS_5VIN_OUT = 1u << 1;
inline constexpr uint8_t M5PM1_IRQ_SYS_5VINOUT_IN = 1u << 2;
inline constexpr uint8_t M5PM1_IRQ_SYS_5VINOUT_OUT = 1u << 3;
inline constexpr uint8_t M5PM1_IRQ_SYS_BATT_IN = 1u << 4;
inline constexpr uint8_t M5PM1_IRQ_SYS_BATT_OUT = 1u << 5;

class M5PM1 {
  // State the chip keeps while the SoC resets: the download lock, the watchdog
  // and the interrupt latches. A power cycle clears the latches, a reset does
  // not.
  struct PersistentState {
    PersistentState()
        : downloadLocked(false),
          watchdogArmed(false),
          watchdogSeconds(0),
          watchdogDeadline(0),
          watchdogCycles(0),
          irqStatus1(0),
          irqStatus2(0),
          irqMask1(0),
          irqMask2(0),
          wakeSource(0),
          gpioFunc1(0),
          gpioWakeEnable(0),
          gpioWakeCfg(0),
          gpioPull1(0),
          gpioDrive(0),
          timerArmed(false),
          timerAction(0),
          timerDeadline(0),
          powerConfig(0),
          rtcRam {} {}

    bool downloadLocked;
    bool watchdogArmed;
    uint8_t watchdogSeconds;
    uint32_t watchdogDeadline;
    // PMIC-driven system power cycles, which is what a watchdog expiry is.
    uint32_t watchdogCycles;
    // REG_IRQ_STATUS1 0x40 and REG_IRQ_STATUS2 0x41 are latches cleared only by
    // a write (write 0 to clear), never by a read.
    uint8_t irqStatus1;
    uint8_t irqStatus2;
    uint8_t irqMask1;
    uint8_t irqMask2;
    uint8_t wakeSource;
    uint8_t gpioFunc1;
    uint8_t gpioWakeEnable;
    uint8_t gpioWakeCfg;
    uint8_t gpioPull1;
    uint8_t gpioDrive;
    bool timerArmed;
    uint8_t timerAction;
    uint32_t timerDeadline;
    // REG_PWR_CFG 0x06: the LED_EN level in bit 4 and the rest of the byte.
    uint8_t powerConfig;
    // The RTC RAM the timed-wake marker lives in. A reset keeps it; a power
    // cycle does not, which is the distinction a timed wake depends on.
    uint8_t rtcRam[8];
  };

  struct Faults {
    Faults()
        : failSetWatchdog(false),
          failReadWatchdog(false),
          failSetDownloadLock(false),
          failReadDownloadLock(false) {}

    bool failSetWatchdog;
    bool failReadWatchdog;
    bool failSetDownloadLock;
    bool failReadDownloadLock;
  };

 public:
  /** Reset the retained PMIC model between independent host tests. */
  static void resetPersistentStateForTest(void) {
    m_Persistent = PersistentState();
    m_Faults = Faults();
    m_PowerCycleHook = nullptr;
  }

  /** Inject one-shot I2C failures for negative-path host tests. */
  static void failNextForTest(bool set_watchdog,
                              bool read_watchdog,
                              bool set_download_lock,
                              bool read_download_lock) {
    m_Faults.failSetWatchdog = set_watchdog;
    m_Faults.failReadWatchdog = read_watchdog;
    m_Faults.failSetDownloadLock = set_download_lock;
    m_Faults.failReadDownloadLock = read_download_lock;
  }

  /**
   * Install the board hook for a watchdog expiry.
   *
   * M5PM1.h REG_WDT_CNT 0x0A documents "system resets when countdown reaches 0",
   * so an expiry is a system power cycle, not a status bit. The simulator has no
   * reset line, so it records the cycle through this hook instead of dying.
   */
  static void setPowerCycleHook(void (*hook)(void)) { m_PowerCycleHook = hook; }

  int begin(void *) {
    m_Shutdown = false;
    m_Idle = true;
    m_BeginMillis = Furble::Sim::clockMillis();
    return M5PM1_OK;
  }

  int setSingleResetDisable(bool value) {
    return access([this, value]() { m_SingleResetDisabled = value; });
  }

  int setDoubleOffDisable(bool value) {
    return access([this, value]() { m_DoubleOffDisabled = value; });
  }

  int setDownloadLock(bool value) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failSetDownloadLock)) {
      return M5PM1_ERROR;
    }
    return access([value]() { m_Persistent.downloadLocked = value; });
  }

  int getDownloadLock(bool *value) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failReadDownloadLock)) {
      return M5PM1_ERROR;
    }
    return access([value]() {
      if (value != nullptr) {
        *value = m_Persistent.downloadLocked;
      }
    });
  }

  // ---- Watchdog: REG_WDT_CNT 0x0A, REG_WDT_KEY 0x0B ----

  int wdtSet(uint8_t timeout_seconds) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failSetWatchdog)) {
      return M5PM1_ERROR;
    }
    return access([timeout_seconds]() {
      m_Persistent.watchdogSeconds = timeout_seconds;
      m_Persistent.watchdogArmed = timeout_seconds != 0;
      m_Persistent.watchdogDeadline = Furble::Sim::clockMillis() + timeout_seconds * 1000U;
    });
  }

  int wdtFeed(void) {
    return access([]() {
      serviceWatchdog();
      if (!m_Persistent.watchdogArmed) {
        return;
      }
      m_Persistent.watchdogDeadline =
          Furble::Sim::clockMillis() + m_Persistent.watchdogSeconds * 1000U;
    });
  }

  int wdtGetCount(uint8_t *count) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failReadWatchdog)) {
      return M5PM1_ERROR;
    }
    return access([count]() {
      serviceWatchdog();
      if (count == nullptr) {
        return;
      }
      if (!m_Persistent.watchdogArmed) {
        *count = 0;
        return;
      }
      const uint32_t remaining = m_Persistent.watchdogDeadline - Furble::Sim::clockMillis();
      *count = static_cast<uint8_t>(std::min<uint32_t>(255, (remaining + 999) / 1000));
    });
  }

  /**
   * Let the modelled chip act on a deadline that has passed.
   *
   * The real PM1 counts down on its own; the host model needs a caller from the
   * platform tick or a query. The SoC re-arms the watchdog after its boot, so
   * the countdown restarts here.
   */
  void pollDeadlines(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    serviceWatchdog();
    if (m_Persistent.timerArmed
        && Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                             m_Persistent.timerDeadline)) {
      m_Persistent.timerArmed = false;
    }
  }

  uint32_t watchdogPowerCycles(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.watchdogCycles;
  }

  // ---- Interrupt latches: REG_IRQ_STATUS1 0x40, REG_IRQ_STATUS2 0x41 ----

  int irqGetGpioStatus(uint8_t *status, uint8_t cleanType = M5PM1_CLEAN_NONE) {
    return access([status, cleanType]() {
      if (status == nullptr) {
        return;
      }
      *status = m_Persistent.irqStatus1;
      // M5PM1.cpp irqGetGpioStatus(): CLEAN_ONCE writes the inverted value, so
      // the bits that were set are cleared and the rest are left alone.
      if ((cleanType == M5PM1_CLEAN_ONCE) && (*status != 0)) {
        writeStatus1(static_cast<uint8_t>(~(*status)));
      } else if (cleanType == M5PM1_CLEAN_ALL) {
        writeStatus1(0);
      }
    });
  }

  int irqGetSysStatus(uint8_t *status, uint8_t cleanType = M5PM1_CLEAN_NONE) {
    return access([status, cleanType]() {
      if (status == nullptr) {
        return;
      }
      *status = m_Persistent.irqStatus2;
      if ((cleanType == M5PM1_CLEAN_ONCE) && (*status != 0)) {
        writeStatus2(static_cast<uint8_t>(~(*status)));
      } else if (cleanType == M5PM1_CLEAN_ALL) {
        writeStatus2(0);
      }
    });
  }

  int irqClearGpioAll(void) {
    return access([]() { writeStatus1(0); });
  }

  int irqClearSysAll(void) {
    return access([]() { writeStatus2(0); });
  }

  int irqSetGpioMask(uint8_t pin, uint8_t mask) {
    return access([pin, mask]() {
      if (mask == M5PM1_IRQ_MASK_ENABLE) {
        m_Persistent.irqMask1 = static_cast<uint8_t>(m_Persistent.irqMask1 | pin);
      } else {
        m_Persistent.irqMask1 = static_cast<uint8_t>(m_Persistent.irqMask1 & ~pin);
      }
    });
  }

  int irqSetSysMask(uint8_t pin, uint8_t mask) {
    return access([pin, mask]() {
      if (mask == M5PM1_IRQ_MASK_ENABLE) {
        m_Persistent.irqMask2 = static_cast<uint8_t>(m_Persistent.irqMask2 | pin);
      } else {
        m_Persistent.irqMask2 = static_cast<uint8_t>(m_Persistent.irqMask2 & ~pin);
      }
    });
  }

  uint8_t irqStatus1(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.irqStatus1;
  }

  uint8_t irqStatus2(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.irqStatus2;
  }

  uint8_t irqMask1(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.irqMask1;
  }

  uint8_t irqMask2(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.irqMask2;
  }

  /**
   * The aggregated IRQ line the PMIC drives into the SoC (PYG1_IRQ on GPIO13).
   *
   * Status 1 and Status 2 share it and it is level active, so an unmasked latch
   * in either register holds the line asserted. Nothing in the motion path
   * clears Status 2, which is why the latch outlives an arm cycle.
   */
  bool irqLineAsserted(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    const uint8_t gpio = static_cast<uint8_t>(m_Persistent.irqStatus1 & ~m_Persistent.irqMask1);
    const uint8_t system = static_cast<uint8_t>(m_Persistent.irqStatus2 & ~m_Persistent.irqMask2);
    return (gpio != 0) || (system != 0);
  }

  /** Latch a system interrupt event, which is a board-level input. */
  void injectSysIrq(uint8_t bits) {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    m_Persistent.irqStatus2 = static_cast<uint8_t>(m_Persistent.irqStatus2 | bits);
  }

  /** Latch a GPIO interrupt edge, which is what the IMU line produces. */
  void injectGpioIrq(uint8_t bits) {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    m_Persistent.irqStatus1 = static_cast<uint8_t>(m_Persistent.irqStatus1 | bits);
  }

  /** Latch a system interrupt, which is what a 5VIN insert or removal does. */
  void raiseSysIrqForTest(uint8_t bits) { injectSysIrq(bits); }

  void raiseGpioIrqForTest(uint8_t bits) { injectGpioIrq(bits); }

  /** The aggregated PYG1_IRQ line, active high while either latch holds a bit. */
  bool irqLineAssertedForTest(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return (m_Persistent.irqStatus1 | m_Persistent.irqStatus2) != 0;
  }

  uint8_t gpioIrqStatusForTest(void) const { return irqStatus1(); }

  uint8_t sysIrqStatusForTest(void) const { return irqStatus2(); }

  uint8_t gpioIrqMaskForTest(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.irqMask1;
  }

  uint8_t gpioFuncForTest(uint8_t pin) const {
    return pin == M5PM1_GPIO_NUM_4 ? gpioFunc4() : 0xFF;
  }

  bool gpioWakeEnabledForTest(uint8_t pin) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return pin == M5PM1_GPIO_NUM_4
           && ((m_Persistent.gpioWakeEnable & static_cast<uint8_t>(1u << pin)) != 0);
  }

  // ---- GPIO configuration, M5PM1.cpp gpioSet() and friends ----

  int gpioSet(uint8_t pin, uint8_t mode, uint8_t value, uint8_t pull, uint8_t drive) {
    return access([this, pin, mode, value, pull, drive]() {
      if (pin > M5PM1_GPIO_NUM_4) {
        return;
      }
      m_GpioMode =
          static_cast<uint8_t>((m_GpioMode & ~(1u << pin)) | ((mode != 0) ? (1u << pin) : 0));
      m_GpioOut =
          static_cast<uint8_t>((m_GpioOut & ~(1u << pin)) | ((value != 0) ? (1u << pin) : 0));
      if (pin == M5PM1_GPIO_NUM_4) {
        // REG_GPIO_PUPD1 0x15 [1:0] and REG_GPIO_DRV 0x13 bit 4.
        m_Persistent.gpioPull1 = static_cast<uint8_t>(pull & 0x03);
        m_Persistent.gpioDrive = static_cast<uint8_t>((m_Persistent.gpioDrive & ~(1u << pin))
                                                      | ((drive != 0) ? (1u << pin) : 0));
      }
    });
  }

  int gpioSetFunc(uint8_t pin, uint8_t func) {
    return access([pin, func]() {
      if (pin != M5PM1_GPIO_NUM_4) {
        return;
      }
      // REG_GPIO_FUNC1 0x17 [1:0].
      m_Persistent.gpioFunc1 = static_cast<uint8_t>(func & 0x03);
    });
  }

  int gpioSetWakeEnable(uint8_t pin, bool enable) {
    return access([pin, enable]() {
      if (enable) {
        m_Persistent.gpioWakeEnable =
            static_cast<uint8_t>(m_Persistent.gpioWakeEnable | (1u << pin));
      } else {
        m_Persistent.gpioWakeEnable =
            static_cast<uint8_t>(m_Persistent.gpioWakeEnable & ~(1u << pin));
      }
    });
  }

  int gpioSetWakeEdge(uint8_t pin, uint8_t edge) {
    return access([pin, edge]() {
      // REG_GPIO_WAKE_CFG 0x19: 0 = falling, 1 = rising.
      if (edge != 0) {
        m_Persistent.gpioWakeCfg = static_cast<uint8_t>(m_Persistent.gpioWakeCfg | (1u << pin));
      } else {
        m_Persistent.gpioWakeCfg = static_cast<uint8_t>(m_Persistent.gpioWakeCfg & ~(1u << pin));
      }
    });
  }

  uint8_t gpioFunc4(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.gpioFunc1;
  }

  uint8_t gpioWakeEnable(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.gpioWakeEnable;
  }

  uint8_t gpioWakeCfg(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.gpioWakeCfg;
  }

  int gpioGetInput(uint8_t pin, uint8_t *value) {
    return access([pin, value]() {
      if (value == nullptr) {
        return;
      }
      // GPIO4 is the IMU interrupt line, pulled up and idle high. A latched
      // GPIO4 interrupt is what pulls it low, which is why the reader sees a
      // level rather than an edge. GPIO0 carries the charge status input, which
      // this model reads as released because the battery gauge reports the
      // charging state instead.
      const bool asserted =
          (pin == M5PM1_GPIO_NUM_4) && ((m_Persistent.irqStatus1 & M5PM1_IRQ_GPIO4) != 0);
      *value = asserted ? 0 : 1;
    });
  }

  // ---- Power configuration: REG_PWR_CFG 0x06, LED_EN level in bit 4 ----

  int getPowerConfig(uint8_t *config) {
    return access([config]() {
      if (config != nullptr) {
        *config = m_Persistent.powerConfig;
      }
    });
  }

  /** M5PM1.cpp setPowerConfig(): one masked read-modify-write of PWR_CFG. */
  int setPowerConfig(uint8_t mask, uint8_t value) {
    return access([mask, value]() {
      m_Persistent.powerConfig =
          static_cast<uint8_t>((m_Persistent.powerConfig & static_cast<uint8_t>(~mask)) | value);
    });
  }

  /** M5PM1.cpp getPowerSource(): REG_PWR_SRC 0x04 bits 2:0, unclamped. */
  int getPowerSource(m5pm1_pwr_src_t *source) {
    return access([source]() {
      if (source != nullptr) {
        *source = M5PM1_PWR_SRC_BAT;
      }
    });
  }

  int setLedEnLevel(bool level) {
    return access([level]() {
      if (level) {
        m_Persistent.powerConfig =
            static_cast<uint8_t>(m_Persistent.powerConfig | M5PM1_PWR_CFG_LED_CTRL);
      } else {
        m_Persistent.powerConfig =
            static_cast<uint8_t>(m_Persistent.powerConfig & ~M5PM1_PWR_CFG_LED_CTRL);
      }
    });
  }

  // ---- RTC RAM: the timed-wake marker the platform writes and reads back ----

  int writeRtcRAM(uint8_t offset, const uint8_t *data, uint8_t len) {
    if ((data == nullptr) || (static_cast<size_t>(offset) + len > sizeof(m_Persistent.rtcRam))) {
      return M5PM1_ERROR;
    }
    return access([offset, data, len]() {
      for (uint8_t i = 0; i < len; i++) {
        m_Persistent.rtcRam[offset + i] = data[i];
      }
    });
  }

  int readRtcRAM(uint8_t offset, uint8_t *data, uint8_t len) {
    if ((data == nullptr) || (static_cast<size_t>(offset) + len > sizeof(m_Persistent.rtcRam))) {
      return M5PM1_ERROR;
    }
    return access([offset, data, len]() {
      for (uint8_t i = 0; i < len; i++) {
        data[i] = m_Persistent.rtcRam[offset + i];
      }
    });
  }

  // ---- Wake source: REG_WAKE_SRC 0x05, write 0 to clear ----

  int getWakeSource(uint8_t *src, uint8_t cleanType = M5PM1_CLEAN_NONE) {
    return access([src, cleanType]() {
      if (src == nullptr) {
        return;
      }
      *src = m_Persistent.wakeSource;
      if ((cleanType == M5PM1_CLEAN_ONCE) && (*src != 0)) {
        m_Persistent.wakeSource = static_cast<uint8_t>(m_Persistent.wakeSource & (*src));
      } else if (cleanType == M5PM1_CLEAN_ALL) {
        m_Persistent.wakeSource = 0;
      }
    });
  }

  // ---- Timer: REG_TIM_CNT 0x38-0x3B, REG_TIM_CFG 0x3C, REG_TIM_KEY 0x3D ----

  int timerSet(uint32_t seconds) { return timerSet(seconds, M5PM1_TIM_ACTION_POWERON); }

  int timerSet(uint32_t seconds, uint8_t action) {
    return access([seconds, action]() {
      // M5PM1.cpp timerSet() writes the count, then CFG with ARM set, then the
      // reload key. A failure can therefore leave a timer armed, which is why
      // the production path calls timerClear() on the error path.
      m_Persistent.timerArmed = seconds != 0;
      m_Persistent.timerAction = action;
      m_Persistent.timerDeadline = Furble::Sim::clockMillis() + seconds * 1000U;
    });
  }

  int timerClear(void) {
    return access([]() {
      m_Persistent.timerArmed = false;
      m_Persistent.timerDeadline = 0;
    });
  }

  bool timerExpired(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.timerArmed
           && Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                                m_Persistent.timerDeadline);
  }

  uint8_t timerAction(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Persistent.timerAction;
  }

  // ---- Power: REG_SYS_CMD 0x0C ----

  int shutdown(void) {
    return access([this]() {
      m_Shutdown = true;
      m_Persistent.watchdogArmed = false;
      m_Persistent.timerArmed = false;
    });
  }

  int readVbat(uint16_t *millivolts) {
    return access([this, millivolts]() {
      if (millivolts != nullptr) {
        constexpr uint32_t DISCHARGE_PERIOD_MS = 3600000;
        constexpr uint16_t START_MILLIVOLTS = 4000;
        constexpr uint16_t END_MILLIVOLTS = 3300;
        const uint32_t elapsed = Furble::Sim::clockMillis() - m_BeginMillis;
        const uint32_t drop =
            std::min<uint32_t>(static_cast<uint32_t>(START_MILLIVOLTS - END_MILLIVOLTS),
                               elapsed * (START_MILLIVOLTS - END_MILLIVOLTS) / DISCHARGE_PERIOD_MS);
        *millivolts = static_cast<uint16_t>(START_MILLIVOLTS - drop);
      }
    });
  }

  int btnGetState(bool *pressed) {
    const int result = access([]() {});
    if (pressed != nullptr) {
      *pressed = false;
    }
    return result;
  }

  bool isShutdown(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return m_Shutdown;
  }

  bool downloadRecoveryUnlocked(void) const {
    const std::lock_guard<std::mutex> guard(m_Mutex);
    return !m_Persistent.downloadLocked;
  }

 private:
  static bool consumeFault(bool &fault) {
    if (!fault) {
      return false;
    }
    fault = false;
    return true;
  }

  /** Write-0-to-clear, M5PM1.h REG_IRQ_STATUS1 0x40. */
  static void writeStatus1(uint8_t value) {
    m_Persistent.irqStatus1 = static_cast<uint8_t>(m_Persistent.irqStatus1 & value);
  }

  /** Write-0-to-clear, M5PM1.h REG_IRQ_STATUS2 0x41. */
  static void writeStatus2(uint8_t value) {
    m_Persistent.irqStatus2 = static_cast<uint8_t>(m_Persistent.irqStatus2 & value);
  }

  /**
   * Act on a watchdog deadline that has passed.
   *
   * REG_WDT_CNT 0x0A documents a system reset at zero, so the model records a
   * power cycle and restarts the countdown, which is what the SoC's boot does
   * on hardware. No flag is left behind for a later reader to interpret.
   */
  static void serviceWatchdog(void) {
    if (!m_Persistent.watchdogArmed) {
      return;
    }
    if (!Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                           m_Persistent.watchdogDeadline)) {
      return;
    }
    m_Persistent.watchdogCycles++;
    m_Persistent.irqStatus1 = 0;
    m_Persistent.irqStatus2 = 0;
    m_Persistent.wakeSource = 0;
    m_Persistent.watchdogDeadline =
        Furble::Sim::clockMillis() + m_Persistent.watchdogSeconds * 1000U;
    if (m_PowerCycleHook != nullptr) {
      m_PowerCycleHook();
    }
  }

  template <typename Callback>
  int access(Callback callback) {
    // The UI task feeds the watchdog while the scenario thread reads state, so
    // one mutex covers every register access and every host-only reader.
    const std::lock_guard<std::mutex> guard(m_Mutex);
    // The first transaction after the PMIC has entered idle only wakes it.
    if (m_Idle) {
      m_Idle = false;
      return M5PM1_ERROR;
    }
    if (m_Shutdown) {
      return M5PM1_ERROR;
    }
    callback();
    m_Idle = true;
    return M5PM1_OK;
  }

  bool m_Idle = true;
  bool m_Shutdown = false;
  bool m_SingleResetDisabled = false;
  bool m_DoubleOffDisabled = false;
  uint8_t m_GpioMode = 0;
  uint8_t m_GpioOut = 0;
  uint32_t m_BeginMillis = 0;

  inline static PersistentState m_Persistent;
  inline static Faults m_Faults;
  inline static void (*m_PowerCycleHook)(void) = nullptr;
  inline static std::mutex m_Mutex;
};

#endif
