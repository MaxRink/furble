#include "FurbleIMU.h"

#include <cstddef>
#include <mutex>

#include <esp_timer.h>

#include <M5Unified.h>

#include "FurblePlatform.h"

namespace Furble {
namespace IMU {

namespace {

// Register addresses and bit fields are from the InvenSense MPU-6886 datasheet
// register map, revision 1.2. The arm sequence matches the M5StickC library's
// enableWakeOnMotion(), which furble cannot call because it uses M5Unified.
constexpr uint8_t IMU_ADDRESS = 0x68;
constexpr uint8_t WHO_AM_I = 0x75;
constexpr uint8_t EXPECTED_WHO_AM_I = 0x19;
constexpr uint8_t ACCEL_CONFIG = 0x1C;
constexpr uint8_t ACCEL_CONFIG_2 = 0x1D;
constexpr uint8_t SAMPLE_RATE_DIVIDER = 0x19;
constexpr uint8_t INT_PIN_CONFIG = 0x37;
constexpr uint8_t INT_ENABLE = 0x38;
constexpr uint8_t INT_STATUS = 0x3A;
constexpr uint8_t WOM_X_THRESHOLD = 0x20;
constexpr uint8_t WOM_Y_THRESHOLD = 0x21;
constexpr uint8_t WOM_Z_THRESHOLD = 0x22;
constexpr uint8_t ACCEL_INTELLIGENCE = 0x69;
constexpr uint8_t POWER_MANAGEMENT_1 = 0x6B;
constexpr uint8_t POWER_MANAGEMENT_2 = 0x6C;
constexpr uint32_t I2C_FREQUENCY = 400000;
constexpr uint32_t POLL_INTERVAL_MS = 1000;
// The MPU6886 has wake on motion but no no-motion engine, so entry into the
// stationary state stays a software decision. The hold matches the software
// backend and the BMI270 no-motion duration exactly.
constexpr uint32_t STATIONARY_HOLD_MS = 60 * 1000;
// One threshold count is 4 mg, so 0x10 is about 64 mg. That is close to the
// BMI270 any-motion default of 83 mg, which keeps the two chips comparable.
constexpr uint8_t WOM_THRESHOLD = 0x10;
// INT_STATUS bits 7, 6 and 5 are WOM_X, WOM_Y and WOM_Z.
constexpr uint8_t WOM_STATUS_MASK = 0xE0;

// M5Unified's values for the registers this engine repurposes, from the init
// table in MPU6886_Class.cpp. disarm() has to put them back or the spirit level
// and the IMU live page keep running at the wake-on-motion sample rate and
// filter after the engine is gone.
constexpr uint8_t M5UNIFIED_ACCEL_CONFIG_2 = 0x00;
constexpr uint8_t M5UNIFIED_SAMPLE_RATE_DIVIDER = 0x03;
// M5Unified leaves INT_PIN_CFG at 0xC0: active low, open drain. Open drain is
// why the wake pin needs a pull-up, and the StickC family has no internal one
// on GPIO35.
constexpr uint8_t M5UNIFIED_INT_PIN_CONFIG = 0xC0;
// INT_PIN_CFG bit 5 latches the interrupt so the pin holds until the status is
// read, instead of emitting a 50 us pulse. The pin is the only motion signal
// this project owns: INT_STATUS at 0x3A is clear-on-read, and M5Unified reads
// it on every IMU update (MPU6886_Class.cpp:253), so the spirit level, the IMU
// live page and the console probe all consume WOM events if the register is
// used as the source of truth.
constexpr uint8_t INT_PIN_CONFIG_LATCH = 0x20;

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

bool writeRegister(uint8_t reg, uint8_t value) {
  if (M5.In_I2C.writeRegister(IMU_ADDRESS, reg, &value, 1, I2C_FREQUENCY)) {
    return true;
  }
  return M5.In_I2C.writeRegister(IMU_ADDRESS, reg, &value, 1, I2C_FREQUENCY);
}

class MPU6886Backend final: public MotionBackend {
 public:
  bool arm(void) override {
    if (!M5.Imu.isEnabled()) {
      return false;
    }

    std::lock_guard<std::mutex> lock(g_IMUMutex);

    uint8_t whoAmI = 0;
    if (!readRegisters(WHO_AM_I, &whoAmI, 1) || (whoAmI != EXPECTED_WHO_AM_I)) {
      return false;
    }
    m_Armed = true;

    uint8_t powerManagement = 0;
    if (!readRegisters(POWER_MANAGEMENT_1, &powerManagement, 1)) {
      return false;
    }

    powerManagement &= 0x8F;
    if (!writeRegister(ACCEL_CONFIG, 0x10) || !writeRegister(POWER_MANAGEMENT_1, powerManagement)
        || !writeRegister(POWER_MANAGEMENT_2, 0x07) || !writeRegister(ACCEL_CONFIG_2, 0x21)) {
      return false;
    }

    uint8_t interruptConfig = 0;
    if (!readRegisters(INT_PIN_CONFIG, &interruptConfig, 1)) {
      return false;
    }
    // Active low, and latched so the line holds for a level-triggered wake
    // source and for the pin read in poll().
    interruptConfig = static_cast<uint8_t>(interruptConfig | 0x80 | INT_PIN_CONFIG_LATCH);

    if (!writeRegister(INT_PIN_CONFIG, interruptConfig) || !writeRegister(INT_ENABLE, 0xE0)
        || !writeRegister(WOM_X_THRESHOLD, WOM_THRESHOLD)
        || !writeRegister(WOM_Y_THRESHOLD, WOM_THRESHOLD)
        || !writeRegister(WOM_Z_THRESHOLD, WOM_THRESHOLD)
        || !writeRegister(ACCEL_INTELLIGENCE, 0xC2) || !writeRegister(SAMPLE_RATE_DIVIDER, 19)) {
      return false;
    }

    if (!readRegisters(INT_STATUS, &interruptConfig, 1)) {
      return false;
    }
    (void)interruptConfig;

    if (!readRegisters(POWER_MANAGEMENT_1, &powerManagement, 1)
        || !writeRegister(POWER_MANAGEMENT_1, static_cast<uint8_t>(powerManagement | 0x20))) {
      return false;
    }

    m_State = MotionState::MOVING;
    m_LastMotion = nowMs();
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

    std::lock_guard<std::mutex> lock(g_IMUMutex);

    if (m_UsesInterrupt) {
      Platform::getInstance().disarmMotionWake();
    }

    writeRegister(INT_ENABLE, 0);
    writeRegister(ACCEL_INTELLIGENCE, 0);

    uint8_t powerManagement = 0;
    if (readRegisters(POWER_MANAGEMENT_1, &powerManagement, 1)) {
      writeRegister(POWER_MANAGEMENT_1, static_cast<uint8_t>(powerManagement & 0xDF));
    }
    writeRegister(POWER_MANAGEMENT_2, 0);

    // Put back everything this engine repurposed. Leaving ACCEL_CONFIG2 and
    // SMPLRT_DIV at the wake-on-motion values would leave the spirit level
    // reading a 16-sample average at 50 Hz for the rest of the session.
    writeRegister(ACCEL_CONFIG_2, M5UNIFIED_ACCEL_CONFIG_2);
    writeRegister(SAMPLE_RATE_DIVIDER, M5UNIFIED_SAMPLE_RATE_DIVIDER);
    writeRegister(INT_PIN_CONFIG, M5UNIFIED_INT_PIN_CONFIG);

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

    // The pin first. INT_STATUS is clear-on-read and M5Unified's IMU update
    // reads it, so on a board where the interrupt reaches a GPIO the pin is the
    // only signal that cannot be consumed by the spirit level or the IMU live
    // page. Where there is no pin, the register is all there is, and an open
    // sensor page can still steal an event: that is what the hardware gate's
    // page-open comparison measures.
    auto &platform = Platform::getInstance();
    bool motion = m_UsesInterrupt && platform.motionWakeAsserted();

    uint8_t status = 0;
    if (!readRegisters(INT_STATUS, &status, 1)) {
      return false;
    }
    motion = motion || ((status & WOM_STATUS_MASK) != 0);

    // Reading INT_STATUS above released the latch, so the wake line is free to
    // assert again and light sleep stays re-entrant.
    if (motion && m_UsesInterrupt) {
      platform.clearMotionWake();
    }

    if (motion) {
      m_LastMotion = now;
      m_InterruptCount++;
      if (m_State != MotionState::MOVING) {
        m_State = MotionState::MOVING;
        state = m_State;
        return true;
      }
      return false;
    }

    if ((m_State == MotionState::MOVING) && ((now - m_LastMotion) >= STATIONARY_HOLD_MS)) {
      m_State = MotionState::STATIONARY;
      state = m_State;
      return true;
    }

    return false;
  }

  MotionState state(void) const override { return m_State; }
  Backend backend(void) const override { return Backend::MPU6886; }
  const char *name(void) const override { return "mpu6886-wom"; }
  bool usesInterrupt(void) const override { return m_UsesInterrupt; }
  uint32_t interruptCount(void) const override { return m_InterruptCount; }

 private:
  MotionState m_State = MotionState::MOVING;
  uint32_t m_LastMotion = 0;
  uint32_t m_LastPoll = 0;
  uint32_t m_InterruptCount = 0;
  bool m_UsesInterrupt = false;
  bool m_Armed = false;
};

}  // namespace

std::unique_ptr<MotionBackend> createMPU6886Backend(void) {
  return std::make_unique<MPU6886Backend>();
}

}  // namespace IMU
}  // namespace Furble
