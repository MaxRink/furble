#include "FurbleIMU.h"

#include <cstddef>
#include <mutex>

#include <esp_timer.h>

#include <M5Unified.h>

#include "FurblePlatform.h"

namespace Furble {
namespace IMU {

namespace {

// Register addresses and bit masks are from the Bosch BMI270 datasheet
// BST-BMI270-DS000 register map, cross-checked against the Bosch
// BMI270_SensorAPI headers named in each comment. The any-motion and no-motion
// feature layout is not in the datasheet. It is only described by that API, so
// the feature offsets and field masks below cite it directly.
constexpr uint8_t IMU_ADDRESS = 0x68;

// bmi2_defs.h BMI2_CHIP_ID_ADDR, datasheet 5.2.1. BMI270 reads 0x24.
constexpr uint8_t CHIP_ID = 0x00;
constexpr uint8_t EXPECTED_CHIP_ID = 0x24;
// bmi2_defs.h BMI2_INT_STATUS_0_ADDR, datasheet 5.2.9.
constexpr uint8_t INTERRUPT_STATUS_0 = 0x1C;
// bmi2_defs.h BMI2_INTERNAL_STATUS_ADDR, datasheet 5.2.14. The low nibble is
// the message field and reads BMI2_INIT_OK once the config file is loaded.
constexpr uint8_t INTERNAL_STATUS = 0x21;
constexpr uint8_t INTERNAL_STATUS_MESSAGE = 0x0F;
constexpr uint8_t INTERNAL_STATUS_INIT_OK = 0x01;
// bmi2_defs.h BMI2_FEAT_PAGE_ADDR and BMI2_FEATURES_REG_ADDR, datasheet 5.2.24.
// The feature window is BMI2_FEAT_SIZE_IN_BYTES wide.
constexpr uint8_t FEATURE_PAGE = 0x2F;
constexpr uint8_t FEATURES_REG = 0x30;
constexpr size_t FEATURE_SIZE = 16;
// bmi2_defs.h BMI2_INT1_IO_CTRL_ADDR, datasheet 5.2.36. Bit 1 lvl, bit 2 od,
// bit 3 output_en. 0x08 is push-pull, active low, output enabled.
constexpr uint8_t INT1_IO_CONTROL = 0x53;
constexpr uint8_t INT1_IO_CONTROL_PUSH_PULL_ACTIVE_LOW = 0x08;
// bmi2_defs.h BMI2_INT_LATCH_ADDR, datasheet 5.2.38. 0x00 is non-latched, so
// the pin releases without a bus transaction and light sleep stays re-entrant.
constexpr uint8_t INTERRUPT_LATCH = 0x55;
// bmi2_defs.h BMI2_INT1_MAP_FEAT_ADDR, datasheet 5.2.39.
constexpr uint8_t INT1_MAP_FEATURE = 0x56;
// bmi2_defs.h BMI2_INT_MAP_DATA_ADDR, datasheet 5.2.40. Bits 3:0 map the data
// interrupts to INT1, bits 7:4 to INT2. M5Unified writes 0xFF here during
// begin() (BMI270_Class.cpp:60), which maps data-ready to both pins. Enabling
// the INT1 output without clearing that turns the wake line into a ~100 Hz
// pulse train, so the wake source fires continuously and light sleep never
// settles. BMI2_DRDY_INT is 0x04 (bmi2_defs.h:1366-1368).
constexpr uint8_t INT_MAP_DATA = 0x58;
constexpr uint8_t INT_MAP_DATA_INT1_MASK = 0x0F;
// bmi270.h BMI270_INT_NO_MOT_MASK and BMI270_INT_ANY_MOT_MASK. The same bit
// assignment is used by INT1_MAP_FEAT and INT_STATUS_0.
constexpr uint8_t NO_MOTION_STATUS = 0x20;
constexpr uint8_t ANY_MOTION_STATUS = 0x40;
// bmi2_defs.h BMI2_PWR_CONF_ADDR and BMI2_ADV_POW_EN_MASK, datasheet 5.2.44.
// The feature window is inaccessible while advanced power save is on, so every
// feature access brackets it the way bmi270.c does around its own feature
// writes. On the M5StickS3 the bracket is inert: M5Unified writes PWR_CONF 0x00
// during begin() (BMI270_Class.cpp:55) and never turns power save back on. It
// is kept because nothing guarantees that stays true, and because a silently
// dropped feature write is indistinguishable on the bench from a dead
// interrupt.
constexpr uint8_t PWR_CONF = 0x7C;
constexpr uint8_t PWR_CONF_ADV_POWER_SAVE = 0x01;
// bmi2_defs.h BMI2_PWR_CTRL_ADDR and BMI2_ACC_EN_MASK, datasheet 5.2.45.
constexpr uint8_t PWR_CTRL = 0x7D;
constexpr uint8_t PWR_CTRL_ACC_EN = 0x04;

constexpr uint32_t I2C_FREQUENCY = 400000;
constexpr uint32_t POLL_INTERVAL_MS = 1000;

// bmi270.h BMI270_ANY_MOT_STRT_ADDR and BMI270_NO_MOT_STRT_ADDR, with the
// pages from the bmi270_feat_in table in bmi270.c.
constexpr uint8_t ANY_MOTION_PAGE = 1;
constexpr uint8_t ANY_MOTION_OFFSET = 0x0C;
constexpr uint8_t NO_MOTION_PAGE = 2;
constexpr uint8_t NO_MOTION_OFFSET = 0x00;

// Field masks from bmi2_defs.h. Word 0 holds the duration in bits 12:0 and one
// select bit per axis. Word 1 holds the slope threshold in bits 10:0 and the
// feature enable in bit 15. Bits 14:11 of word 1 are the output configuration.
// Bosch never writes them, so neither does this code: they keep whatever the
// config file M5Unified uploaded put there.
constexpr uint16_t DURATION_MASK = 0x1FFF;
constexpr uint16_t AXIS_SELECT_MASK = 0xE000;
constexpr uint16_t THRESHOLD_MASK = 0x07FF;
constexpr uint16_t FEATURE_ENABLE = 0x8000;

// One duration count is 20 ms and one threshold count is 0.48 mg, both stated
// by the Bosch examples bmi270_examples/any_motion_interrupt and
// no_motion_interrupt.
//
// Any-motion uses the shortest duration the chip offers, because exit from
// stationary has to be immediate. Its threshold is the Bosch default of 83 mg.
constexpr uint16_t ANY_MOTION_DURATION = 1;
constexpr uint16_t ANY_MOTION_THRESHOLD = 0xAA;
// No-motion holds for 60 s, matching the software backend exactly so the two
// paths report the same state at the same time. Its threshold is the Bosch
// default of 70 mg.
constexpr uint16_t NO_MOTION_DURATION = 3000;
constexpr uint16_t NO_MOTION_THRESHOLD = 0x090;

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

// The IMU shares the internal bus with the PMIC, which fails its first
// transaction after idle sleep. Retry once, the same way Platform::m5pm1Access
// does, so a wake from light sleep does not lose the access that follows it.
bool readRegisters(uint8_t reg, uint8_t *data, size_t length) {
  if (M5.In_I2C.readRegister(IMU_ADDRESS, reg, data, length, I2C_FREQUENCY)) {
    return true;
  }
  return M5.In_I2C.readRegister(IMU_ADDRESS, reg, data, length, I2C_FREQUENCY);
}

bool writeRegisters(uint8_t reg, const uint8_t *data, size_t length) {
  if (M5.In_I2C.writeRegister(IMU_ADDRESS, reg, data, length, I2C_FREQUENCY)) {
    return true;
  }
  return M5.In_I2C.writeRegister(IMU_ADDRESS, reg, data, length, I2C_FREQUENCY);
}

bool writeRegister(uint8_t reg, uint8_t value) {
  return writeRegisters(reg, &value, 1);
}

uint16_t readWord(const uint8_t *feature, uint8_t offset) {
  return static_cast<uint16_t>(feature[offset] | (feature[offset + 1] << 8));
}

void writeWord(uint8_t *feature, uint8_t offset, uint16_t word) {
  feature[offset] = static_cast<uint8_t>(word & 0xFF);
  feature[offset + 1] = static_cast<uint8_t>(word >> 8);
}

/**
 * Read one feature page, apply a change to the two words at @p offset, and
 * write the page back. Only the documented fields are touched.
 */
template <typename T>
bool updateFeature(uint8_t page, uint8_t offset, T &&change) {
  uint8_t feature[FEATURE_SIZE] = {};
  if (!writeRegister(FEATURE_PAGE, page) || !readRegisters(FEATURES_REG, feature, FEATURE_SIZE)) {
    return false;
  }

  uint16_t word0 = readWord(feature, offset);
  uint16_t word1 = readWord(feature, offset + 2);
  change(word0, word1);
  writeWord(feature, offset, word0);
  writeWord(feature, offset + 2, word1);

  return writeRegisters(FEATURES_REG, feature, FEATURE_SIZE);
}

bool setFeatureConfig(uint8_t page, uint8_t offset, uint16_t duration, uint16_t threshold) {
  return updateFeature(page, offset, [duration, threshold](uint16_t &word0, uint16_t &word1) {
    word0 = static_cast<uint16_t>((duration & DURATION_MASK) | AXIS_SELECT_MASK);
    word1 = static_cast<uint16_t>((word1 & ~THRESHOLD_MASK) | (threshold & THRESHOLD_MASK));
  });
}

bool setFeatureEnabled(uint8_t page, uint8_t offset, bool enable) {
  return updateFeature(page, offset, [enable](uint16_t &, uint16_t &word1) {
    if (enable) {
      word1 |= FEATURE_ENABLE;
    } else {
      word1 &= static_cast<uint16_t>(~FEATURE_ENABLE);
    }
  });
}

class BMI270Backend final: public MotionBackend {
 public:
  bool arm(void) override {
    if (!M5.Imu.isEnabled()) {
      return false;
    }

    // The spirit level and the IMU live page share this bus, and everything
    // below is read-modify-write. Held for the whole sequence, never across a
    // delay.
    std::lock_guard<std::mutex> lock(g_IMUMutex);

    uint8_t chipId = 0;
    if (!readRegisters(CHIP_ID, &chipId, 1) || (chipId != EXPECTED_CHIP_ID)) {
      return false;
    }

    // The feature engine only runs once M5Unified's config file upload has
    // completed and the accelerometer is on.
    uint8_t internalStatus = 0;
    if (!readRegisters(INTERNAL_STATUS, &internalStatus, 1)
        || ((internalStatus & INTERNAL_STATUS_MESSAGE) != INTERNAL_STATUS_INIT_OK)) {
      return false;
    }

    uint8_t powerControl = 0;
    if (!readRegisters(PWR_CTRL, &powerControl, 1)) {
      return false;
    }
    if (((powerControl & PWR_CTRL_ACC_EN) == 0)
        && !writeRegister(PWR_CTRL, static_cast<uint8_t>(powerControl | PWR_CTRL_ACC_EN))) {
      return false;
    }

    m_Armed = true;

    uint8_t map = 0;
    if (!readRegisters(INT1_MAP_FEATURE, &map, 1)
        || !readRegisters(INT_MAP_DATA, &m_DataMapRestore, 1) || !beginFeatureAccess()) {
      return false;
    }

    const bool configured =
        // Take data-ready off INT1 before the output is enabled, or the wake
        // line carries the accelerometer sample rate instead of motion events.
        writeRegister(INT_MAP_DATA,
                      static_cast<uint8_t>(m_DataMapRestore & ~INT_MAP_DATA_INT1_MASK))
        && writeRegister(INT1_IO_CONTROL, INT1_IO_CONTROL_PUSH_PULL_ACTIVE_LOW)
        && writeRegister(INTERRUPT_LATCH, 0x00)
        && writeRegister(INT1_MAP_FEATURE,
                         static_cast<uint8_t>(map | ANY_MOTION_STATUS | NO_MOTION_STATUS))
        && setFeatureConfig(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, ANY_MOTION_DURATION,
                            ANY_MOTION_THRESHOLD)
        && setFeatureConfig(NO_MOTION_PAGE, NO_MOTION_OFFSET, NO_MOTION_DURATION,
                            NO_MOTION_THRESHOLD)
        && setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, false)
        && setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, true);
    endFeatureAccess();

    if (!configured) {
      return false;
    }

    // Discard any status latched before the engines were configured.
    uint8_t status = 0;
    if (!readRegisters(INTERRUPT_STATUS_0, &status, 1)) {
      return false;
    }

    m_State = MotionState::MOVING;
    m_LastPoll = 0;
    m_InterruptCount = 0;
    m_UsesInterrupt = Platform::getInstance().armMotionWake();
    return true;
  }

  void disarm(void) override {
    if (!m_Armed) {
      return;
    }

    std::lock_guard<std::mutex> lock(g_IMUMutex);

    if (m_UsesInterrupt) {
      Platform::getInstance().disarmMotionWake();
    }

    // Hand data-ready back to M5Unified exactly as it was found.
    writeRegister(INT_MAP_DATA, m_DataMapRestore);

    if (beginFeatureAccess()) {
      setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, false);
      setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, false);
      endFeatureAccess();
    }
    m_UsesInterrupt = false;
    m_Armed = false;
  }

  bool poll(MotionState &state) override {
    const uint32_t now = nowMs();
    if ((now - m_LastPoll) < POLL_INTERVAL_MS) {
      return false;
    }
    m_LastPoll = now;

    std::lock_guard<std::mutex> lock(g_IMUMutex);

    uint8_t status = 0;
    if (!readRegisters(INTERRUPT_STATUS_0, &status, 1)) {
      return false;
    }

    // Exactly one engine is armed at a time, so the status bit that fires
    // identifies the transition without further reads.
    const bool toStationary =
        (m_State == MotionState::MOVING) && ((status & NO_MOTION_STATUS) != 0);
    const bool toMoving =
        (m_State == MotionState::STATIONARY) && ((status & ANY_MOTION_STATUS) != 0);
    if (!toStationary && !toMoving) {
      return false;
    }

    if (!swapEngines(toStationary)) {
      return false;
    }

    // The PMIC latches its GPIO IRQ status. Left set it holds PYG1_IRQ
    // asserted, and a level-triggered wake source that never releases stops
    // light sleep entirely.
    if (m_UsesInterrupt) {
      Platform::getInstance().clearMotionWake();
    }

    m_InterruptCount++;
    m_State = toStationary ? MotionState::STATIONARY : MotionState::MOVING;
    state = m_State;
    return true;
  }

  MotionState state(void) const override { return m_State; }
  Backend backend(void) const override { return Backend::BMI270; }
  const char *name(void) const override { return "bmi270-motion"; }
  bool usesInterrupt(void) const override { return m_UsesInterrupt; }
  uint32_t interruptCount(void) const override { return m_InterruptCount; }

 private:
  /** Disable advanced power save so the feature window is accessible. */
  bool beginFeatureAccess(void) {
    uint8_t powerConfig = 0;
    if (!readRegisters(PWR_CONF, &powerConfig, 1)) {
      return false;
    }
    m_PowerSaveRestore = (powerConfig & PWR_CONF_ADV_POWER_SAVE) != 0;
    if (!m_PowerSaveRestore) {
      return true;
    }
    return writeRegister(PWR_CONF, static_cast<uint8_t>(powerConfig & ~PWR_CONF_ADV_POWER_SAVE));
  }

  /** Restore advanced power save to the value found by beginFeatureAccess. */
  void endFeatureAccess(void) {
    if (!m_PowerSaveRestore) {
      return;
    }
    uint8_t powerConfig = 0;
    if (readRegisters(PWR_CONF, &powerConfig, 1)) {
      writeRegister(PWR_CONF, static_cast<uint8_t>(powerConfig | PWR_CONF_ADV_POWER_SAVE));
    }
    m_PowerSaveRestore = false;
  }

  /** Arm any-motion and disarm no-motion, or the reverse. */
  bool swapEngines(bool toStationary) {
    if (!beginFeatureAccess()) {
      return false;
    }
    const bool ok = setFeatureEnabled(toStationary ? NO_MOTION_PAGE : ANY_MOTION_PAGE,
                                      toStationary ? NO_MOTION_OFFSET : ANY_MOTION_OFFSET, false)
                    && setFeatureEnabled(toStationary ? ANY_MOTION_PAGE : NO_MOTION_PAGE,
                                         toStationary ? ANY_MOTION_OFFSET : NO_MOTION_OFFSET, true);
    endFeatureAccess();
    return ok;
  }

  MotionState m_State = MotionState::MOVING;
  uint32_t m_LastPoll = 0;
  uint32_t m_InterruptCount = 0;
  uint8_t m_DataMapRestore = 0;
  bool m_UsesInterrupt = false;
  bool m_Armed = false;
  bool m_PowerSaveRestore = false;
};

}  // namespace

std::unique_ptr<MotionBackend> createBMI270Backend(void) {
  return std::make_unique<BMI270Backend>();
}

}  // namespace IMU
}  // namespace Furble
