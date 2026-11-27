#ifndef FURBLE_PLATFORM_SHIM_M5PM1_H
#define FURBLE_PLATFORM_SHIM_M5PM1_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

// Host double for the M5Stack M5PM1 driver.
//
// The register values below are the vendor ones (M5PM1 1.0.6, src/M5PM1.h), so
// the production static_asserts that pin the policy enumeration to them still
// run here. Only the registers the platform touches are modelled, behind a
// register-bus fake in the style of lib/hal/AXP2101.h and
// tests/host/hal_drivers_test.cpp: every write is recorded, so a test can assert
// that a refused transition wrote nothing at all.
//
// The timed wake needs two more pieces of the part, and both are modelled
// rather than stubbed to a constant: WAKE_SRC 0x05, which is read-to-clear, and
// the 32-byte RTC buffer the wake marker and the predicted-clock record live
// in. The buffer is the store the PM1 keeps while it stays powered, so bytes
// written before a shutdown are still readable after one. `platform-timed-wake`
// drives the production Platform against both.

inline constexpr int M5PM1_OK = 0;
inline constexpr int M5PM1_ERROR = -1;

// M5PM1_REG_PWR_SRC 0x04 bits 2:0 holds the vendor m5pm1_pwr_src_t numbering.
// The enumeration is declared below, with the same enumerator names as M5PM1
// 1.0.6, so the production static_asserts run against it unchanged.

// M5PM1_REG_PWR_CFG 0x06 bits.
inline constexpr uint8_t M5PM1_PWR_CFG_CHG_EN = 0x01;
inline constexpr uint8_t M5PM1_PWR_CFG_DCDC_EN = 0x02;
inline constexpr uint8_t M5PM1_PWR_CFG_LDO_EN = 0x04;
inline constexpr uint8_t M5PM1_PWR_CFG_BOOST_EN = 0x08;
inline constexpr uint8_t M5PM1_PWR_CFG_LED_CTRL = 0x10;

// Register addresses used by the double.
inline constexpr uint8_t M5PM1_REG_PWR_SRC = 0x04;
inline constexpr uint8_t M5PM1_REG_PWR_CFG = 0x06;

// GPIO and IRQ selectors, as plain ints: the double does not need the vendor
// enumerations, only the values the platform passes.
inline constexpr int M5PM1_GPIO_NUM_0 = 0;
inline constexpr int M5PM1_GPIO_NUM_4 = 4;
inline constexpr int M5PM1_GPIO_MODE_INPUT = 0;
inline constexpr int M5PM1_GPIO_PULL_UP = 1;
inline constexpr int M5PM1_GPIO_DRIVE_PUSHPULL = 0;
inline constexpr int M5PM1_GPIO_FUNC_IRQ = 1;
inline constexpr int M5PM1_GPIO_WAKE_FALLING = 1;
inline constexpr int M5PM1_IRQ_GPIO4 = 4;
inline constexpr int M5PM1_IRQ_MASK_DISABLE = 0;
inline constexpr int M5PM1_IRQ_MASK_ENABLE = 1;
inline constexpr int M5PM1_CLEAN_NONE = 0;
inline constexpr int M5PM1_CLEAN_ONCE = 1;
inline constexpr int M5PM1_TIM_ACTION_POWERON = 0;

// M5PM1_REG_WAKE_SRC 0x05 flags (vendor m5pm1_wake_src_t).
inline constexpr uint8_t M5PM1_WAKE_SRC_TIM = 0x01;
inline constexpr uint8_t M5PM1_WAKE_SRC_VIN = 0x02;
inline constexpr uint8_t M5PM1_WAKE_SRC_PWRBTN = 0x04;
inline constexpr uint8_t M5PM1_WAKE_SRC_RSTBTN = 0x08;
inline constexpr uint8_t M5PM1_WAKE_SRC_CMD_RST = 0x10;
inline constexpr uint8_t M5PM1_WAKE_SRC_EXT_WAKE = 0x20;
inline constexpr uint8_t M5PM1_WAKE_SRC_5VINOUT = 0x40;

// Vendor enumeration, M5PM1 1.0.6 src/M5PM1.h m5pm1_pwr_src_t. The register
// reports three bits, so the double also declares the values the vendor
// enumeration does not name: tests/host/CLAUDE.md requires a host double to
// keep a production type's full value range, and UBSan rejects a value the
// stub does not declare. The platform is what clamps 4 to 7 to UNKNOWN.
typedef enum {
  M5PM1_PWR_SRC_5VIN = 0,     // USB/DC 5 V input
  M5PM1_PWR_SRC_5VINOUT = 1,  // 5 V bidirectional port input
  M5PM1_PWR_SRC_BAT = 2,      // battery only
  M5PM1_PWR_SRC_UNKNOWN = 3,  // no supply reported
  M5PM1_PWR_SRC_UNDEFINED_4 = 4,
  M5PM1_PWR_SRC_UNDEFINED_5 = 5,
  M5PM1_PWR_SRC_UNDEFINED_6 = 6,
  M5PM1_PWR_SRC_UNDEFINED_7 = 7,
} m5pm1_pwr_src_t;

class M5PM1 {
 public:
  M5PM1() { s_Instance = this; }

  /**
   * The double the linked Platform holds.
   *
   * Platform owns one M5PM1 by value, so the test reaches the register model
   * through this registry rather than through a second instance of its own.
   */
  static M5PM1 &instanceForTest(void) { return *s_Instance; }

  int begin(void *i2c) {
    (void)i2c;
    return M5PM1_OK;
  }

  int setSingleResetDisable(bool) { return M5PM1_OK; }
  int setDoubleOffDisable(bool) { return M5PM1_OK; }

  int setDownloadLock(bool lock) {
    if (m_FailNext) {
      m_FailNext = false;
      return M5PM1_ERROR;
    }
    m_DownloadLocked = lock;
    return M5PM1_OK;
  }

  int getDownloadLock(bool *lock) {
    if (lock != nullptr) {
      *lock = m_DownloadLocked;
    }
    return M5PM1_OK;
  }

  int wdtSet(uint8_t) { return M5PM1_OK; }
  int wdtFeed(void) { return M5PM1_OK; }
  int wdtGetCount(uint8_t *count) {
    if (count != nullptr) {
      *count = 0;
    }
    return M5PM1_OK;
  }

  int readVbat(uint16_t *mv) {
    if (mv != nullptr) {
      *mv = 4000;
    }
    return M5PM1_OK;
  }

  int shutdown(void) { return M5PM1_OK; }

  int gpioSet(int, int, uint8_t, int, int) { return M5PM1_OK; }
  int gpioSetFunc(int, int) { return M5PM1_OK; }
  int irqClearGpioAll(void) { return M5PM1_OK; }
  // #488 clears IRQ Status 2 as well, so the fake answers both.
  int irqClearSysAll(void) { return M5PM1_OK; }
  int irqSetGpioMask(int, int) { return M5PM1_OK; }
  int gpioSetWakeEnable(int, bool) { return M5PM1_OK; }
  int gpioSetWakeEdge(int, int) { return M5PM1_OK; }
  int gpioGetInput(int, uint8_t *value) {
    if (value != nullptr) {
      *value = 0;
    }
    return M5PM1_OK;
  }
  int irqGetGpioStatus(uint8_t *status, int) {
    if (status != nullptr) {
      *status = 0;
    }
    return M5PM1_OK;
  }
  int getWakeSource(uint8_t *source, int clean) {
    if (source == nullptr) {
      return M5PM1_ERROR;
    }
    if (!accessOk()) {
      return M5PM1_ERROR;
    }
    *source = m_WakeSource;
    // M5PM1_CLEAN_ONCE latches the vendor behaviour the production boot relies
    // on: the register is read and cleared, so a second boot cannot see the
    // wake source of the first.
    if (clean == M5PM1_CLEAN_ONCE) {
      m_WakeSource = 0;
    }
    return M5PM1_OK;
  }
  int writeRtcRAM(uint8_t offset, const uint8_t *data, uint8_t len) {
    if ((data == nullptr) || (static_cast<unsigned>(offset) + len > sizeof(m_RtcRam))) {
      return M5PM1_ERROR;
    }
    if ((m_RtcRamWriteFailures > 0) && (offset == m_RtcRamWriteFailOffset)) {
      m_RtcRamWriteFailures--;
      return M5PM1_ERROR;
    }
    std::memcpy(&m_RtcRam[offset], data, len);
    m_RtcRamWrites.push_back({offset, len});
    return M5PM1_OK;
  }
  int readRtcRAM(uint8_t offset, uint8_t *data, uint8_t len) {
    if ((data == nullptr) || (static_cast<unsigned>(offset) + len > sizeof(m_RtcRam))) {
      return M5PM1_ERROR;
    }
    if ((m_RtcRamReadFailures > 0) && (offset == m_RtcRamReadFailOffset)) {
      m_RtcRamReadFailures--;
      return M5PM1_ERROR;
    }
    std::memcpy(data, &m_RtcRam[offset], len);
    return M5PM1_OK;
  }
  int timerSet(uint32_t seconds, int) {
    m_TimerSeconds = seconds;
    return M5PM1_OK;
  }
  int timerClear(void) {
    m_TimerSeconds = 0;
    return M5PM1_OK;
  }
  int btnGetState(bool *pressed) {
    if (pressed != nullptr) {
      *pressed = false;
    }
    return M5PM1_OK;
  }

  /** Read PWR_CFG 0x06. */
  int getPowerConfig(uint8_t *config) {
    if (config == nullptr) {
      return M5PM1_ERROR;
    }
    if (!accessOk()) {
      return M5PM1_ERROR;
    }
    *config = m_PowerConfig;
    return M5PM1_OK;
  }

  /**
   * Masked read-modify-write of PWR_CFG 0x06, as the vendor driver does it.
   *
   * M5PM1 1.0.6 setPowerConfig() reads the register, masks and writes it back
   * as two separate transactions, which is why the platform serializes the
   * whole update. The double keeps that shape and lets a test place work
   * between the two transactions through setInterleaveHook().
   */
  int setPowerConfig(uint8_t mask, uint8_t value) {
    if (!accessOk()) {
      return M5PM1_ERROR;
    }
    const uint8_t current = m_PowerConfig;
    if (m_Interleave != nullptr) {
      m_Interleave();
    }
    m_PowerConfig = static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask));
    m_PowerConfigWrites.push_back(m_PowerConfig);
    m_RegisterWrites.emplace_back(M5PM1_REG_PWR_CFG, m_PowerConfig);
    return M5PM1_OK;
  }

  /** Read PWR_SRC 0x04 bits 2:0, unclamped exactly like the vendor driver. */
  int getPowerSource(m5pm1_pwr_src_t *source) {
    if (source == nullptr) {
      return M5PM1_ERROR;
    }
    if (!accessOk() || m_PowerSourceUnreadable) {
      return M5PM1_ERROR;
    }
    *source = static_cast<m5pm1_pwr_src_t>(m_PowerSource & 0x07);
    return M5PM1_OK;
  }

  // ---- test controls -------------------------------------------------

  /** Set the raw PWR_SRC 0x04 low nibble, 0 to 7. */
  void setPowerSourceRaw(uint8_t value) { m_PowerSource = static_cast<uint8_t>(value & 0x07); }
  void setPowerSourceUnreadable(bool unreadable) { m_PowerSourceUnreadable = unreadable; }
  void setPowerConfig(uint8_t value) { m_PowerConfig = value; }
  uint8_t powerConfig(void) const { return m_PowerConfig; }
  /** Every PWR_CFG 0x06 write the platform made, in order. */
  const std::vector<uint8_t> &powerConfigWrites(void) const { return m_PowerConfigWrites; }
  const std::vector<std::pair<uint8_t, uint8_t>> &registerWrites(void) const {
    return m_RegisterWrites;
  }
  /** Run a callback between the read and the write of setPowerConfig(). */
  void setInterleaveHook(std::function<void()> hook) { m_Interleave = std::move(hook); }
  void clearWrites(void) {
    m_PowerConfigWrites.clear();
    m_RegisterWrites.clear();
  }

  /**
   * Set WAKE_SRC 0x05 as the next boot would report it.
   *
   * The vendor register reports every source that brought the rail back, so a
   * test sets the whole byte rather than a flag.
   */
  void setWakeSourceRaw(uint8_t value) { m_WakeSource = value; }
  uint8_t wakeSource(void) const { return m_WakeSource; }

  /**
   * The 32-byte RTC buffer, as M5PM1 v1.0.6 documents it.
   *
   * A plain array is the faithful model here for the one property the gate
   * depends on: the PM1 keeps this buffer while it stays powered, and it is the
   * PM1 that powers the ESP32 off and back on. A host process outlives the
   * simulated power-off exactly as the PM1 does, so bytes written before the
   * shutdown are still here after it. The buffer is *not* a model of retention
   * across a PM1 power loss (a battery pull), which no host test can show.
   */
  const uint8_t *rtcRam(void) const { return m_RtcRam; }
  void clearRtcRam(void) {
    std::memset(m_RtcRam, 0, sizeof(m_RtcRam));
    m_RtcRamWrites.clear();
  }
  /** Every RTC buffer write the platform made, as (offset, length). */
  const std::vector<std::pair<uint8_t, uint8_t>> &rtcRamWrites(void) const {
    return m_RtcRamWrites;
  }
  /**
   * Fail the next `failures` reads of the RTC buffer at `offset`.
   *
   * Offset-scoped because the boot reads two regions of the same buffer, the
   * four-byte marker at 0 and the record at 4, and the case worth testing is the
   * marker readable while the record is not. Production retries once per
   * transaction, so a test that wants a failed read asks for two.
   */
  void failRtcRamReadsAt(uint8_t offset, unsigned failures) {
    m_RtcRamReadFailOffset = offset;
    m_RtcRamReadFailures = failures;
  }

  /**
   * Fail the next `failures` writes of the RTC buffer at `offset`.
   *
   * Scoped the same way as the read failures. Two models one transaction that
   * fails and then succeeds, which is what separates "the write did not land" from
   * "the bus is gone": the first is the case the shutdown has to answer for.
   */
  void failRtcRamWritesAt(uint8_t offset, unsigned failures) {
    m_RtcRamWriteFailOffset = offset;
    m_RtcRamWriteFailures = failures;
  }

  /** The interval the last timerSet() armed, in seconds. */
  uint32_t timerSeconds(void) const { return m_TimerSeconds; }
  /** Forget the armed interval, as a part that has just powered up would. */
  void clearTimer(void) { m_TimerSeconds = 0; }

 private:
  bool accessOk(void) {
    if (m_FailAllAccess) {
      return false;
    }
    return true;
  }

  inline static M5PM1 *s_Instance = nullptr;
  std::function<void()> m_Interleave;
  bool m_FailNext = false;
  bool m_FailAllAccess = false;
  bool m_DownloadLocked = true;
  uint8_t m_PowerConfig = 0x17;  // vendor documented default
  uint8_t m_PowerSource = M5PM1_PWR_SRC_BAT;
  bool m_PowerSourceUnreadable = false;
  std::vector<uint8_t> m_PowerConfigWrites;
  std::vector<std::pair<uint8_t, uint8_t>> m_RegisterWrites;
  uint8_t m_WakeSource = 0;
  uint8_t m_RtcRam[32] = {};
  std::vector<std::pair<uint8_t, uint8_t>> m_RtcRamWrites;
  uint8_t m_RtcRamWriteFailOffset = 0;
  unsigned m_RtcRamWriteFailures = 0;
  uint8_t m_RtcRamReadFailOffset = 0;
  unsigned m_RtcRamReadFailures = 0;
  uint32_t m_TimerSeconds = 0;
};

#endif
