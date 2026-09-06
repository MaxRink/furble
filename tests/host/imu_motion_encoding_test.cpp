// Register-level coverage for the BMI270 and MPU6886 motion engines.
//
// The simulator models what the engines mean, not what they write: it has no
// I2C bus, and modelling the register semantics there would be a second
// implementation of the thing under test. So the wire side is pinned here
// instead, against a recording bus, and every expected value carries its
// source.
//
// BMI270 register addresses and bit masks come from the Bosch BMI270 datasheet
// BST-BMI270-DS000 register map, cross-checked against the Bosch
// BMI270_SensorAPI headers. The any-motion and no-motion feature layout is not
// in the datasheet at all. It is described only by that API:
//
//   bmi270.h        BMI270_ANY_MOT_STRT_ADDR 0x0C, BMI270_NO_MOT_STRT_ADDR 0x00,
//                   BMI270_INT_NO_MOT_MASK 0x20, BMI270_INT_ANY_MOT_MASK 0x40
//   bmi270.c        bmi270_feat_in: ANY_MOTION on page 1, NO_MOTION on page 2
//   bmi2_defs.h     BMI2_ANY_NO_MOT_DUR_MASK 0x1FFF, X/Y/Z_SEL 0x2000/0x4000/0x8000,
//                   BMI2_ANY_NO_MOT_THRES_MASK 0x07FF, BMI2_ANY_NO_MOT_EN_MASK 0x80
//                   at byte offset BMI2_ANY_MOT_FEAT_EN_OFFSET 0x03
//   bmi2.c          set_any_motion_config / set_no_motion_config write duration and
//                   the axis selects into word 0 and the threshold into bits 10:0 of
//                   word 1, and never touch bits 14:11
//   bmi2.c          bmi2_set_regs saves PWR_CONF.adv_power_save, clears it around the
//                   write and restores it
//
// The unit scales and the defaults are stated by the Bosch examples
// bmi270_examples/any_motion_interrupt/any_motion_interrupt.c and
// bmi270_examples/no_motion_interrupt/no_motion_interrupt.c: one duration count
// is 20 ms, one threshold count is 0.48 mg, the any-motion default threshold is
// 83 mg and the no-motion default threshold is 70 mg.
//
// MPU6886 values come from the InvenSense MPU-6886 datasheet register map.

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <M5Unified.h>

#include "FurbleIMU.h"
#include "FurblePlatform.h"

namespace {

constexpr uint8_t IMU_ADDRESS = 0x68;
constexpr uint8_t FEAT_PAGE = 0x2F;
constexpr uint8_t FEATURES = 0x30;
constexpr size_t FEATURE_SIZE = 16;

int g_Failures = 0;
int g_Checks = 0;

bool check(bool condition, const std::string &message) {
  g_Checks++;
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

bool checkEqual(unsigned actual, unsigned expected, const std::string &message) {
  const bool ok = actual == expected;
  if (!ok) {
    std::cerr << "  (expected 0x" << std::hex << expected << ", got 0x" << actual << std::dec
              << ")\n";
  }
  return check(ok, message);
}

/** One recorded bus transaction. */
struct Access {
  bool write;
  uint8_t reg;
  std::vector<uint8_t> data;
};

/**
 * A register file that behaves the way the parts do: a flat map for ordinary
 * registers, plus eight sixteen byte feature pages selected by FEAT_PAGE.
 */
struct Bus {
  std::map<uint8_t, uint8_t> registers;
  std::array<std::array<uint8_t, FEATURE_SIZE>, 8> pages = {};
  std::vector<Access> log;
  bool imuEnabled = true;
  uint8_t page = 0;
  // Number of leading transactions to fail, which is how the shared bus behaves
  // after the PMIC's idle sleep: the first one wakes it and reports failure.
  int failNext = 0;

  void reset(void) {
    registers.clear();
    pages = {};
    log.clear();
    page = 0;
  }

  std::vector<Access> writesTo(uint8_t reg) const {
    std::vector<Access> found;
    for (const auto &access : log) {
      if (access.write && (access.reg == reg)) {
        found.push_back(access);
      }
    }
    return found;
  }

  size_t writeIndex(uint8_t reg, size_t occurrence = 0) const {
    size_t seen = 0;
    for (size_t index = 0; index < log.size(); index++) {
      if (log[index].write && (log[index].reg == reg)) {
        if (seen == occurrence) {
          return index;
        }
        seen++;
      }
    }
    return log.size();
  }

  uint16_t featureWord(uint8_t p, uint8_t offset) const {
    return static_cast<uint16_t>(pages[p][offset] | (pages[p][offset + 1] << 8));
  }
};

Bus g_Bus;
int64_t g_Micros = 0;

}  // namespace

int64_t esp_timer_get_time(void) {
  return g_Micros;
}

namespace HostI2C {

bool imuEnabled(void) {
  return g_Bus.imuEnabled;
}

bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  if (address != IMU_ADDRESS) {
    return false;
  }
  if (g_Bus.failNext > 0) {
    g_Bus.failNext--;
    g_Bus.log.push_back({false, reg, {}});
    return false;
  }
  if ((reg == FEATURES) && (length == FEATURE_SIZE)) {
    std::memcpy(data, g_Bus.pages[g_Bus.page].data(), FEATURE_SIZE);
  } else {
    for (size_t index = 0; index < length; index++) {
      data[index] = g_Bus.registers[static_cast<uint8_t>(reg + index)];
    }
  }
  g_Bus.log.push_back({false, reg, std::vector<uint8_t>(data, data + length)});
  return true;
}

bool write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
  if (address != IMU_ADDRESS) {
    return false;
  }
  if (g_Bus.failNext > 0) {
    g_Bus.failNext--;
    g_Bus.log.push_back({true, reg, {}});
    return false;
  }
  g_Bus.log.push_back({true, reg, std::vector<uint8_t>(data, data + length)});
  if ((reg == FEATURES) && (length == FEATURE_SIZE)) {
    std::memcpy(g_Bus.pages[g_Bus.page].data(), data, FEATURE_SIZE);
    return true;
  }
  for (size_t index = 0; index < length; index++) {
    g_Bus.registers[static_cast<uint8_t>(reg + index)] = data[index];
  }
  if (reg == FEAT_PAGE) {
    g_Bus.page = data[0] & 0x07;
  }
  return true;
}

}  // namespace HostI2C

HostM5 M5;

namespace Furble {

// The firmware defines this in FurbleUI.cpp or main.cpp depending on the build.
// The engines take it around every register sequence, so the harness owns one.
imu_mutex_t g_IMUMutex;

namespace IMU {

// The engines count their bus retries through MotionSource, which lives in a
// translation unit this target deliberately does not link: the point here is
// the register encoding, not the source's state machine.
std::atomic<uint32_t> MotionSource::s_BusRetries {0};
std::atomic<float> MotionSource::s_Scale {1.0f};

void MotionSource::noteBusRetry(void) {
  s_BusRetries.fetch_add(1, std::memory_order_relaxed);
}

uint32_t MotionSource::busRetries(void) {
  return s_BusRetries.load(std::memory_order_relaxed);
}

}  // namespace IMU

Platform &Platform::getInstance(void) {
  static Platform instance;
  return instance;
}

bool Platform::armMotionWake(void) {
  wakeArmed = wakeAvailable;
  return wakeArmed;
}

void Platform::disarmMotionWake(void) {
  wakeArmed = false;
}

bool Platform::motionWakeAsserted(void) const {
  return wakeArmed && wakeAsserted;
}

void Platform::clearMotionWake(void) {
  if (wakeArmed) {
    wakeClears++;
  }
}

uint32_t Platform::motionWakeEdges(void) const {
  return edges;
}

uint32_t Platform::getM5PM1RetryCount(void) const {
  return 0;
}

}  // namespace Furble

namespace {

using Furble::IMU::MotionState;

void seedBMI270(void) {
  g_Bus.reset();
  g_Bus.registers[0x00] = 0x24;  // CHIP_ID
  g_Bus.registers[0x21] = 0x01;  // INTERNAL_STATUS, message BMI2_INIT_OK
  g_Bus.registers[0x7C] = 0x01;  // PWR_CONF, advanced power save on
  g_Bus.registers[0x7D] = 0x04;  // PWR_CTRL, acc_en already set
  // M5Unified maps data ready to both interrupt pins during begin().
  g_Bus.registers[0x58] = 0xFF;  // INT_MAP_DATA
  // The config file M5Unified uploads leaves an output configuration in bits
  // 14:11 of each threshold word. Bosch never rewrites it, so neither may we.
  g_Bus.pages[1][0x0E] = 0x00;
  g_Bus.pages[1][0x0F] = 0x38;  // out_conf 7 in bits 14:11
  g_Bus.pages[2][0x02] = 0x00;
  g_Bus.pages[2][0x03] = 0x30;  // out_conf 6 in bits 14:11
}

// Test 1. The BMI270 arm sequence, in order, with the documented values.
void testBMI270Arm(void) {
  std::cerr << "test: BMI270 arm writes the datasheet register sequence\n";
  seedBMI270();

  auto backend = Furble::IMU::createBMI270Backend();
  check(backend != nullptr, "the BMI270 factory returns a backend");
  check(backend->arm(), "the BMI270 backend arms on a chip that identifies itself");

  check(g_Bus.log.size() > 0 && !g_Bus.log[0].write && (g_Bus.log[0].reg == 0x00),
        "the first transaction reads CHIP_ID");

  const auto io = g_Bus.writesTo(0x53);
  check(io.size() == 1 && io[0].data[0] == 0x08,
        "INT1_IO_CTRL is push-pull, active low, output enabled");
  const auto latch = g_Bus.writesTo(0x55);
  check(latch.size() == 1 && latch[0].data[0] == 0x00,
        "INT_LATCH is non-latched so the pin releases without a bus transaction");
  const auto map = g_Bus.writesTo(0x56);
  check(map.size() == 1 && map[0].data[0] == 0x60,
        "INT1_MAP_FEAT routes any-motion and no-motion to INT1");

  // Without this the wake line carries M5Unified's data-ready mapping, which is
  // the accelerometer sample rate, and the wake source never stops firing.
  const auto dataMap = g_Bus.writesTo(0x58);
  check(!dataMap.empty(), "INT_MAP_DATA is written");
  if (!dataMap.empty()) {
    checkEqual(dataMap[0].data[0] & 0x0F, 0, "data-ready is taken off INT1 before arming");
    checkEqual(dataMap[0].data[0] & 0xF0, 0xF0, "the INT2 data mapping is left alone");
    check(g_Bus.writeIndex(0x58, 0) < g_Bus.writeIndex(0x53),
          "data-ready is unmapped before the INT1 output is enabled");
  }

  // Advanced power save must be cleared before the feature window is touched
  // and restored afterwards, exactly as bmi2_set_regs does.
  const auto power = g_Bus.writesTo(0x7C);
  check(power.size() == 2, "PWR_CONF is written twice, once to clear and once to restore");
  if (power.size() == 2) {
    checkEqual(power[0].data[0] & 0x01, 0, "advanced power save is cleared first");
    checkEqual(power[1].data[0] & 0x01, 1, "advanced power save is restored afterwards");
    check(g_Bus.writeIndex(0x7C, 0) < g_Bus.writeIndex(FEATURES),
          "the clear happens before the first feature write");
    check(g_Bus.writeIndex(0x7C, 1) > g_Bus.writeIndex(FEATURES),
          "the restore happens after the feature writes");
  }

  // Any-motion: 20 ms, all three axes, 83 mg.
  const uint16_t anyDuration = g_Bus.featureWord(1, 0x0C);
  const uint16_t anyThreshold = g_Bus.featureWord(1, 0x0E);
  checkEqual(anyDuration & 0x1FFF, 1, "any-motion duration is one count, 20 ms");
  checkEqual(anyDuration & 0xE000, 0xE000, "any-motion selects all three axes");
  checkEqual(anyThreshold & 0x07FF, 0xAA, "any-motion threshold is the Bosch default 83 mg");
  checkEqual((anyThreshold >> 11) & 0x0F, 7, "any-motion output configuration is preserved");

  // No-motion: 60 s, all three axes, 70 mg. The duration has to match the
  // software backend's hold exactly or the two disagree on the same device.
  const uint16_t noDuration = g_Bus.featureWord(2, 0x00);
  const uint16_t noThreshold = g_Bus.featureWord(2, 0x02);
  checkEqual(noDuration & 0x1FFF, 3000, "no-motion duration is 3000 counts, 60 s");
  checkEqual(noDuration & 0xE000, 0xE000, "no-motion selects all three axes");
  checkEqual(noThreshold & 0x07FF, 0x090, "no-motion threshold is the Bosch default 70 mg");
  checkEqual((noThreshold >> 11) & 0x0F, 6, "no-motion output configuration is preserved");

  // Exactly one engine is armed. While moving, that is no-motion.
  checkEqual(anyThreshold & 0x8000, 0, "any-motion starts disabled");
  checkEqual(noThreshold & 0x8000, 0x8000, "no-motion starts enabled");

  check(Furble::Platform::getInstance().wakeArmed, "arming takes the light-sleep wake source");
  check(backend->usesInterrupt(), "the backend reports the interrupt path");
  check(backend->state() == MotionState::MOVING, "the initial state is moving");
}

// Test 2. A full transition cycle swaps exactly one engine each way.
void testBMI270Cycle(void) {
  std::cerr << "test: BMI270 swaps one engine per transition\n";
  seedBMI270();
  Furble::Platform::getInstance().wakeClears = 0;
  auto backend = Furble::IMU::createBMI270Backend();
  backend->arm();

  g_Micros = 2000000;
  g_Bus.registers[0x1C] = 0x20;  // INT_STATUS_0, no-motion
  MotionState state = MotionState::MOVING;
  check(backend->poll(state), "the no-motion status raises a transition");
  check(state == MotionState::STATIONARY, "no-motion reports stationary");
  checkEqual(g_Bus.featureWord(2, 0x02) & 0x8000, 0, "no-motion is disabled after it fired");
  checkEqual(g_Bus.featureWord(1, 0x0E) & 0x8000, 0x8000, "any-motion is armed while stationary");
  checkEqual(backend->interruptCount(), 1, "the transition is counted");

  g_Micros = 4000000;
  g_Bus.registers[0x1C] = 0x40;  // INT_STATUS_0, any-motion
  check(backend->poll(state), "the any-motion status raises a transition");
  check(state == MotionState::MOVING, "any-motion reports moving");
  checkEqual(g_Bus.featureWord(1, 0x0E) & 0x8000, 0, "any-motion is disabled after it fired");
  checkEqual(g_Bus.featureWord(2, 0x02) & 0x8000, 0x8000, "no-motion is armed again while moving");
  checkEqual(backend->interruptCount(), 2, "the second transition is counted");

  // The threshold and duration fields survive every enable toggle.
  checkEqual(g_Bus.featureWord(1, 0x0C) & 0x1FFF, 1, "any-motion duration survives the toggles");
  checkEqual(g_Bus.featureWord(2, 0x02) & 0x07FF, 0x090,
             "no-motion threshold survives the toggles");

  check(Furble::Platform::getInstance().wakeClears == 2,
        "each consumed transition clears the latched PMIC wake status");

  backend->disarm();
  checkEqual(g_Bus.featureWord(1, 0x0E) & 0x8000, 0, "disarm disables any-motion");
  checkEqual(g_Bus.featureWord(2, 0x02) & 0x8000, 0, "disarm disables no-motion");
  check(!Furble::Platform::getInstance().wakeArmed, "disarm releases the wake source");
  const auto restored = g_Bus.writesTo(0x58);
  check(!restored.empty() && restored.back().data[0] == 0xFF,
        "disarm hands the data-ready mapping back to M5Unified");

  // Handing data ready back to a pin that is still driving republishes the
  // ~100 Hz pulse train arm() went out of its way to remove, so the output has
  // to be disabled first. The previous version of this test asserted only that
  // the mapping came back and so canonised the asymmetry.
  const auto io1 = g_Bus.writesTo(0x53);
  check(io1.size() == 2, "INT1_IO_CTRL is written on arm and again on disarm");
  if (io1.size() == 2) {
    checkEqual(io1[1].data[0], 0x00, "disarm clears the INT1 output enable");
    check(g_Bus.writeIndex(0x53, 1) < g_Bus.writeIndex(0x58, 1),
          "the INT1 output is disabled before the data-ready mapping returns");
  }
}

// Test 3. The BMI270 refuses to arm on a part that is not ready.
void testBMI270Refusals(void) {
  std::cerr << "test: BMI270 refuses a wrong chip or an unfinished init\n";

  seedBMI270();
  g_Bus.registers[0x00] = 0x19;  // an MPU6886 answering on the same address
  check(!Furble::IMU::createBMI270Backend()->arm(), "a foreign chip id refuses to arm");

  seedBMI270();
  g_Bus.registers[0x21] = 0x00;  // config file upload not finished
  check(!Furble::IMU::createBMI270Backend()->arm(), "an unfinished internal status refuses to arm");

  seedBMI270();
  g_Bus.registers[0x7D] = 0x00;  // acc_en clear
  auto backend = Furble::IMU::createBMI270Backend();
  check(backend->arm(), "a stopped accelerometer is started rather than refused");
  checkEqual(g_Bus.registers[0x7D] & 0x04, 0x04, "PWR_CTRL acc_en is set before arming");
}

void seedMPU6886(void) {
  g_Bus.reset();
  g_Bus.registers[0x75] = 0x19;  // WHO_AM_I
  g_Bus.registers[0x6B] = 0x41;  // PWR_MGMT_1 with bits the arm sequence must clear
  g_Bus.registers[0x37] = 0xC0;  // INT_PIN_CFG as M5Unified leaves it: active low, open drain
}

// Test 4. The MPU6886 wake-on-motion sequence.
void testMPU6886Arm(void) {
  std::cerr << "test: MPU6886 arm writes the datasheet wake-on-motion sequence\n";
  seedMPU6886();

  auto backend = Furble::IMU::createMPU6886Backend();
  check(backend != nullptr, "the MPU6886 factory returns a backend");
  check(backend->arm(), "the MPU6886 backend arms on a chip that identifies itself");

  check(!g_Bus.log.empty() && !g_Bus.log[0].write && (g_Bus.log[0].reg == 0x75),
        "the first transaction reads WHO_AM_I");

  const std::vector<std::pair<uint8_t, uint8_t>> expected = {
      {0x1C, 0x10}, // ACCEL_CONFIG, ACCEL_FS_SEL 2, plus or minus 8 g
      {0x6C, 0x07}, // PWR_MGMT_2, all three gyro axes off
      {0x1D, 0x21}, // ACCEL_CONFIG2, dec2_cfg 2, a_dlpf_cfg 1
      {0x38, 0xE0}, // INT_ENABLE, WOM on x, y and z
      {0x20, 0x10}, // ACCEL_WOM_X_THR, 16 counts at 4 mg is about 64 mg
      {0x21, 0x10}, // ACCEL_WOM_Y_THR
      {0x22, 0x10}, // ACCEL_WOM_Z_THR
      {0x69, 0xC2}, // ACCEL_INTEL_CTRL, enable, mode, WOM_TH_MODE 2
      {0x19, 19  }, // SMPLRT_DIV, 50 Hz
  };
  for (const auto &pair : expected) {
    const auto writes = g_Bus.writesTo(pair.first);
    check(!writes.empty(), "register 0x" + std::to_string(pair.first) + " is written");
    if (!writes.empty()) {
      checkEqual(writes[0].data[0], pair.second,
                 "register 0x" + std::to_string(pair.first) + " gets its datasheet value");
    }
  }

  // PWR_MGMT_1: SLEEP, CYCLE and GYRO_STANDBY cleared first, CYCLE set last.
  const auto power = g_Bus.writesTo(0x6B);
  check(power.size() == 2, "PWR_MGMT_1 is written twice");
  if (power.size() == 2) {
    checkEqual(power[0].data[0] & 0x70, 0, "the first write clears sleep, cycle and gyro standby");
    checkEqual(power[1].data[0] & 0x20, 0x20, "the last write enters accelerometer cycle mode");
    check(g_Bus.writeIndex(0x6B, 1) > g_Bus.writeIndex(0x69),
          "cycle mode is entered after the WOM registers are written");
  }

  // INT_PIN_CFG: active low, and latched. The latch is what makes the pin
  // readable at all: a 50 us pulse cannot be seen by a 1 Hz poll, and it is
  // marginal for the level-triggered light-sleep wake latch too.
  const auto pin = g_Bus.writesTo(0x37);
  check(!pin.empty(), "INT_PIN_CFG is written");
  if (!pin.empty()) {
    checkEqual(pin[0].data[0] & 0x80, 0x80, "INT_PIN_CFG sets ACTL, active low");
    checkEqual(pin[0].data[0] & 0x20, 0x20, "INT_PIN_CFG latches the interrupt");
    checkEqual(pin[0].data[0] & 0x40, 0x40, "the open-drain bit M5Unified set is preserved");
  }

  check(backend->usesInterrupt(), "the backend reports the interrupt path");
}

// Test 5. The MPU6886 has no no-motion engine, so stationary is a timed
// decision on the same 60 s the other two backends use.
void testMPU6886Timing(void) {
  std::cerr << "test: MPU6886 holds 60 s before reporting stationary\n";
  seedMPU6886();
  auto backend = Furble::IMU::createMPU6886Backend();
  g_Micros = 0;
  backend->arm();

  MotionState state = MotionState::MOVING;
  g_Micros = 30000000;  // 30 s of quiet
  g_Bus.registers[0x3A] = 0x00;
  check(!backend->poll(state), "30 s of quiet is not yet stationary");

  g_Micros = 61000000;  // past 60 s
  check(backend->poll(state), "60 s of quiet reports stationary");
  check(state == MotionState::STATIONARY, "the reported state is stationary");

  g_Micros = 62000000;
  g_Bus.registers[0x3A] = 0x80;  // WOM_X
  check(backend->poll(state), "a wake-on-motion status leaves stationary");
  check(state == MotionState::MOVING, "the reported state is moving");

  backend->disarm();
  const auto enable = g_Bus.writesTo(0x38);
  check(!enable.empty() && enable.back().data[0] == 0, "disarm clears INT_ENABLE");
  const auto intel = g_Bus.writesTo(0x69);
  check(!intel.empty() && intel.back().data[0] == 0, "disarm clears ACCEL_INTEL_CTRL");

  // Everything the engine repurposed goes back to M5Unified's init values, or
  // the spirit level keeps reading a 16-sample average at 50 Hz afterwards.
  const auto config2 = g_Bus.writesTo(0x1D);
  check(!config2.empty() && config2.back().data[0] == 0x00,
        "disarm restores ACCEL_CONFIG2 to M5Unified's 0x00");
  const auto divider = g_Bus.writesTo(0x19);
  check(!divider.empty() && divider.back().data[0] == 0x03,
        "disarm restores SMPLRT_DIV to M5Unified's 3");
  const auto pinBack = g_Bus.writesTo(0x37);
  check(!pinBack.empty() && pinBack.back().data[0] == 0xC0,
        "disarm restores INT_PIN_CFG to M5Unified's 0xC0");
}

// The interrupt pin, not the status register, is the motion signal on a board
// where the interrupt reaches a GPIO. INT_STATUS is clear-on-read and
// M5Unified's IMU update reads it, so anything that opens the spirit level or
// the IMU live page consumes WOM events.
void testMPU6886UsesThePin(void) {
  std::cerr << "test: MPU6886 reads the interrupt pin, not just the stolen status\n";
  seedMPU6886();
  auto &platform = Furble::Platform::getInstance();
  platform.wakeClears = 0;
  auto backend = Furble::IMU::createMPU6886Backend();
  g_Micros = 0;
  backend->arm();
  check(backend->usesInterrupt(), "the wake path is armed");

  MotionState state = MotionState::MOVING;
  g_Micros = 61000000;
  g_Bus.registers[0x3A] = 0x00;
  platform.wakeAsserted = false;
  check(backend->poll(state), "60 s of quiet with the pin idle reports stationary");
  check(state == MotionState::STATIONARY, "the state is stationary");

  // The status register has already been consumed by an IMU update elsewhere,
  // so only the pin carries the event.
  g_Micros = 62000000;
  g_Bus.registers[0x3A] = 0x00;
  platform.wakeAsserted = true;
  check(backend->poll(state), "an asserted pin reports motion with a cleared status register");
  check(state == MotionState::MOVING, "the state is moving");
  check(platform.wakeClears > 0, "consuming the event clears the latched wake status");
}

// A first failed transaction is retried once. The bus fails the leading access
// on purpose, which is what the shared internal bus does after the PMIC's idle
// sleep, and the backend still has to arm.
void testRetryOnceHasTeeth(void) {
  std::cerr << "test: the first failed bus access after idle sleep is retried\n";

  seedMPU6886();
  g_Bus.failNext = 1;
  check(Furble::IMU::createMPU6886Backend()->arm(),
        "the MPU6886 engine arms through one failed leading transaction");

  seedBMI270();
  g_Bus.failNext = 1;
  check(Furble::IMU::createBMI270Backend()->arm(),
        "the BMI270 engine arms through one failed leading transaction");

  // Two consecutive failures are a real fault and must not be papered over.
  seedMPU6886();
  g_Bus.failNext = 2;
  check(!Furble::IMU::createMPU6886Backend()->arm(),
        "two consecutive failures refuse to arm rather than retrying forever");
}

}  // namespace

int main(void) {
  testBMI270Arm();
  testBMI270Cycle();
  testBMI270Refusals();
  testMPU6886Arm();
  testMPU6886Timing();
  testMPU6886UsesThePin();
  testRetryOnceHasTeeth();

  std::cerr << g_Checks << " checks, " << g_Failures << " failures\n";
  return (g_Failures == 0) ? 0 : 1;
}
