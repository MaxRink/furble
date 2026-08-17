#include "FurbleIMU.h"

#include <cstddef>

#include <esp_timer.h>

#include <M5Unified.h>

#include "FurblePlatform.h"

namespace Furble {
namespace IMU {

namespace {

constexpr uint8_t IMU_ADDRESS = 0x68;
constexpr uint8_t CHIP_ID = 0x00;
constexpr uint8_t EXPECTED_CHIP_ID = 0x24;
constexpr uint8_t FEATURE_PAGE = 0x2F;
constexpr uint8_t FEATURES_REG = 0x30;
constexpr uint8_t INTERRUPT_STATUS_0 = 0x1C;
constexpr uint8_t INT1_IO_CONTROL = 0x53;
constexpr uint8_t INTERRUPT_LATCH = 0x55;
constexpr uint8_t INT1_MAP_FEATURE = 0x56;
constexpr uint32_t I2C_FREQUENCY = 400000;
constexpr uint32_t POLL_INTERVAL_MS = 1000;

constexpr uint8_t ANY_MOTION_STATUS = 1U << 6;
constexpr uint8_t NO_MOTION_STATUS = 1U << 5;
constexpr uint8_t FEATURE_ENABLE = 1U << 15;
constexpr uint16_t ANY_MOTION_WORD_0 = 0xE001;
constexpr uint16_t ANY_MOTION_WORD_1 = 0xB8AA;
constexpr uint16_t NO_MOTION_WORD_0 = 0xEBB8;
constexpr uint16_t NO_MOTION_WORD_1 = 0xB690;

// These offsets and words are from the Bosch BMI270 sensor API feature map.
// M5Unified uploads the feature configuration before this backend starts.
constexpr uint8_t ANY_MOTION_PAGE = 1;
constexpr uint8_t ANY_MOTION_OFFSET = 0x0C;
constexpr uint8_t NO_MOTION_PAGE = 2;
constexpr uint8_t NO_MOTION_OFFSET = 0x00;

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool readRegisters(uint8_t reg, uint8_t *data, size_t length) {
  return M5.In_I2C.readRegister(IMU_ADDRESS, reg, data, length, I2C_FREQUENCY);
}

bool writeRegister(uint8_t reg, uint8_t value) {
  return M5.In_I2C.writeRegister(IMU_ADDRESS, reg, &value, 1, I2C_FREQUENCY);
}

bool setFeatureWords(uint8_t page, uint8_t offset, uint16_t word0, uint16_t word1) {
  uint8_t feature[16] = {};
  if (!writeRegister(FEATURE_PAGE, page)
      || !readRegisters(FEATURES_REG, feature, sizeof(feature))) {
    return false;
  }

  feature[offset] = static_cast<uint8_t>(word0 & 0xFF);
  feature[offset + 1] = static_cast<uint8_t>(word0 >> 8);
  feature[offset + 2] = static_cast<uint8_t>(word1 & 0xFF);
  feature[offset + 3] = static_cast<uint8_t>(word1 >> 8);
  return M5.In_I2C.writeRegister(IMU_ADDRESS, FEATURES_REG, feature, sizeof(feature),
                                 I2C_FREQUENCY);
}

bool setFeatureEnabled(uint8_t page, uint8_t offset, uint16_t word0, uint16_t word1, bool enable) {
  if (enable) {
    word1 |= FEATURE_ENABLE;
  } else {
    word1 &= static_cast<uint16_t>(~FEATURE_ENABLE);
  }
  return setFeatureWords(page, offset, word0, word1);
}

class BMI270Backend final: public MotionBackend {
 public:
  bool arm(void) override {
    if (!M5.Imu.isEnabled()) {
      return false;
    }

    uint8_t chipId = 0;
    if (!readRegisters(CHIP_ID, &chipId, 1) || (chipId != EXPECTED_CHIP_ID)) {
      return false;
    }
    m_Armed = true;

    uint8_t map = 0;
    if (!readRegisters(INT1_MAP_FEATURE, &map, 1)) {
      return false;
    }

    if (!writeRegister(INT1_IO_CONTROL, 0x08) || !writeRegister(INTERRUPT_LATCH, 0x00)
        || !writeRegister(INT1_MAP_FEATURE, static_cast<uint8_t>(map | 0x60))
        || !setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, ANY_MOTION_WORD_0,
                              ANY_MOTION_WORD_1, false)
        || !setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, NO_MOTION_WORD_0, NO_MOTION_WORD_1,
                              true)) {
      return false;
    }

    uint8_t status = 0;
    if (!readRegisters(INTERRUPT_STATUS_0, &status, 1)) {
      return false;
    }
    (void)status;

    m_State = MotionState::MOVING;
    m_LastPoll = 0;
    m_InterruptCount = 0;
    m_UsesInterrupt = Platform::getInstance().armMotionWake();
    m_Armed = true;
    return true;
  }

  void disarm(void) override {
    if (!m_Armed) {
      return;
    }

    if (m_UsesInterrupt) {
      Platform::getInstance().disarmMotionWake();
    }

    setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, ANY_MOTION_WORD_0, ANY_MOTION_WORD_1,
                      false);
    setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, NO_MOTION_WORD_0, NO_MOTION_WORD_1, false);
    m_UsesInterrupt = false;
    m_Armed = false;
  }

  bool poll(MotionState &state) override {
    const uint32_t now = nowMs();
    if ((now - m_LastPoll) < POLL_INTERVAL_MS) {
      return false;
    }
    m_LastPoll = now;

    uint8_t status = 0;
    if (!readRegisters(INTERRUPT_STATUS_0, &status, 1)) {
      return false;
    }

    if ((m_State == MotionState::MOVING) && ((status & NO_MOTION_STATUS) != 0)) {
      if (!setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, NO_MOTION_WORD_0, NO_MOTION_WORD_1,
                             false)
          || !setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, ANY_MOTION_WORD_0,
                                ANY_MOTION_WORD_1, true)) {
        return false;
      }
      m_InterruptCount++;
      m_State = MotionState::STATIONARY;
      state = m_State;
      return true;
    }

    if ((m_State == MotionState::STATIONARY) && ((status & ANY_MOTION_STATUS) != 0)) {
      if (!setFeatureEnabled(ANY_MOTION_PAGE, ANY_MOTION_OFFSET, ANY_MOTION_WORD_0,
                             ANY_MOTION_WORD_1, false)
          || !setFeatureEnabled(NO_MOTION_PAGE, NO_MOTION_OFFSET, NO_MOTION_WORD_0,
                                NO_MOTION_WORD_1, true)) {
        return false;
      }
      m_InterruptCount++;
      m_State = MotionState::MOVING;
      state = m_State;
      return true;
    }

    return false;
  }

  MotionState state(void) const override { return m_State; }
  Backend backend(void) const override { return Backend::BMI270; }
  const char *name(void) const override { return "bmi270-motion"; }
  bool usesInterrupt(void) const override { return m_UsesInterrupt; }
  uint32_t interruptCount(void) const override { return m_InterruptCount; }

 private:
  MotionState m_State = MotionState::MOVING;
  uint32_t m_LastPoll = 0;
  uint32_t m_InterruptCount = 0;
  bool m_UsesInterrupt = false;
  bool m_Armed = false;
};

}  // namespace

std::unique_ptr<MotionBackend> createBMI270Backend(void) {
  return std::make_unique<BMI270Backend>();
}

}  // namespace IMU
}  // namespace Furble
