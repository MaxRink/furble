#include "FurbleLilyGo.h"

#include "FurbleBoardCaps.h"

/*
 * The board I/O side of the LilyGO seam: the input pins, the keyboard bus and
 * the rail bring-up. src/FurbleLilyGo.cpp keeps the family facts and the pure
 * decoders so the host test can pin them without a bus or a GPIO.
 *
 * Nothing here runs on an M5 board, and nothing here runs in a host test: the
 * whole file is behind the LilyGO capability macro plus the simulator arm,
 * which is why it may include the driver headers.
 */
#if FURBLE_LILYGO_BOARD

#include <cstring>

#if defined(FURBLE_SIM) || defined(FURBLE_LILYGO_NO_DEVICE_IO)

// Two builds take this arm: the simulator, which models the input devices
// through its scenario driver and has no I2C bus or GPIO, and the host test,
// which pins the latch. Both only reset and latch state; the decoders themselves
// live in src/FurbleLilyGo.cpp and are tested directly.
namespace Furble {
namespace LilyGo {

bool Buttons::begin(void) {
  m_Levels = 0;
  m_Pending = 0;
  m_Releasing = 0;
  m_Held = 0;
  m_Samples = 0;
  return true;
}

void Buttons::sample(void) {
  ++m_Samples;
}

bool powerOn(void) {
  return true;
}

}  // namespace LilyGo
}  // namespace Furble

#else  // FURBLE_SIM || FURBLE_LILYGO_NO_DEVICE_IO

#include <driver/gpio.h>
#include <lgfx/v1/platforms/common.hpp>

#include "AXP2101.h"
#include "XL9555.h"

namespace Furble {
namespace LilyGo {
namespace {

constexpr uint32_t I2C_FREQ = 400000;
// Poll the keyboard even when its interrupt line is quiet: 20 samples at the
// UI's 5 ms active cycle is 100 ms, so a missed edge delays a key by at most a
// tenth of a second instead of losing it.
constexpr uint32_t KEYBOARD_POLL_SAMPLES = 20;
// The T-Deck keyboard controller runs its bus at 100 kHz: the vendor's
// Keyboard_T_Deck_Master example calls Wire.begin(SDA, SCL) and its slave
// firmware calls Wire.begin(0x55, SDA, SCL, 100000UL). A faster clock is a
// bench check, not a guess to make here.
constexpr uint32_t KEYBOARD_I2C_FREQ = 100000;

void configureInputPin(int8_t pin) {
  if (pin < 0) {
    return;
  }
  const auto gpio = static_cast<gpio_num_t>(pin);
  (void)gpio_set_direction(gpio, GPIO_MODE_INPUT);
  // The direction switches and the side button are passive switches to ground:
  // the vendor firmware configures them as INPUT_PULLUP and reads a low pin as
  // pressed, and the trackball center switch shares the BOOT strapping pin,
  // which already has the board's external pull-up.
  (void)gpio_pullup_en(gpio);
}

bool readPin(int8_t pin) {
  if (pin < 0) {
    return false;
  }
  return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
}

/**
 * The AXP2101 and the XL9555 on the T-Watch Ultra's I2C bus.
 *
 * Uses the pure register drivers in lib/hal, which carry the datasheet
 * citations, and the same lgfx I2C transport the panel uses. Only the rails
 * this port draws from are switched: ALDO2 (display), ALDO1 (SD card), BLDO2
 * (speaker amplifier) and BLDO1 (GNSS), plus VRTC which has no enable bit and
 * keeps the receiver's backup alive. LDO/DLDO rails furble does not use are
 * left as the vendor firmware leaves them.
 */
class AxpBus: public HAL::RegisterBus {
 public:
  explicit AxpBus(uint8_t port) : m_Port(port) {}

  bool read(uint8_t reg, uint8_t *data, size_t length) override {
    if ((data == nullptr) || (length == 0)) {
      return false;
    }
    return lgfx::i2c::readRegister(m_Port, HAL::AXP2101::ADDRESS, reg, data, length, I2C_FREQ)
        .has_value();
  }

  bool write(uint8_t reg, const uint8_t *data, size_t length) override {
    if ((data == nullptr) || (length == 0) || (length >= (sizeof(m_Packet) - 1))) {
      return false;
    }
    m_Packet[0] = reg;
    memcpy(&m_Packet[1], data, length);
    return lgfx::i2c::transactionWrite(m_Port, HAL::AXP2101::ADDRESS, m_Packet,
                                       static_cast<uint8_t>(length + 1), I2C_FREQ)
        .has_value();
  }

 private:
  uint8_t m_Port;
  uint8_t m_Packet[8] = {};
};

class ExpanderBus: public HAL::RegisterBus {
 public:
  explicit ExpanderBus(uint8_t port) : m_Port(port) {}

  bool read(uint8_t reg, uint8_t *data, size_t length) override {
    if ((data == nullptr) || (length == 0)) {
      return false;
    }
    return lgfx::i2c::readRegister(m_Port, HAL::XL9555::ADDRESS_BASE, reg, data, length, I2C_FREQ)
        .has_value();
  }

  bool write(uint8_t reg, const uint8_t *data, size_t length) override {
    if ((data == nullptr) || (length == 0) || (length >= (sizeof(m_Packet) - 1))) {
      return false;
    }
    m_Packet[0] = reg;
    memcpy(&m_Packet[1], data, length);
    return lgfx::i2c::transactionWrite(m_Port, HAL::XL9555::ADDRESS_BASE, m_Packet,
                                       static_cast<uint8_t>(length + 1), I2C_FREQ)
        .has_value();
  }

 private:
  uint8_t m_Port;
  uint8_t m_Packet[8] = {};
};

/** XL9555 pin 7 is the panel supply enable (LilyGoLib EXPANDS_DISP_EN). */
constexpr uint8_t EXPANDER_DISPLAY_ENABLE = 7;

bool bringUpUltra(void) {
  const Facts &facts = factsFor(BoardId::TWATCH_ULTRA);
  if (!lgfx::i2c::init(facts.i2c.port, facts.i2c.sda, facts.i2c.scl).has_value()) {
    return false;
  }

  AxpBus axp_bus(facts.i2c.port);
  // No rail is protected here: this is board bring-up, not a power backend
  // that hands rails to a holder, so the mask that would keep one on is empty.
  HAL::AXP2101 pmic(axp_bus, 0);
  if (!pmic.probe()) {
    return false;
  }

  struct RailPlan {
    HAL::AXP2101::Rail rail;
    uint16_t millivolts;
  };
  // LilyGoLib docs/hardware/lilygo-t-watch-ultra.md, "Power Management
  // Channels": ALDO1 SD, ALDO2 display, BLDO1 GNSS, BLDO2 speaker.
  static constexpr RailPlan RAILS[] = {
      {HAL::AXP2101::Rail::ALDO2, 3300},
      {HAL::AXP2101::Rail::ALDO1, 3300},
      {HAL::AXP2101::Rail::BLDO2, 3300},
      {HAL::AXP2101::Rail::BLDO1, 3300},
  };
  for (const RailPlan &plan : RAILS) {
    if (!pmic.setLdoVoltage(plan.rail, plan.millivolts) || !pmic.setRail(plan.rail, true)) {
      return false;
    }
  }

  ExpanderBus expander_bus(facts.i2c.port);
  HAL::XL9555 expander(expander_bus);
  return expander.setOutput(EXPANDER_DISPLAY_ENABLE, true);
}

}  // namespace

bool powerOn(void) {
  const Facts &f = facts();
  if (f.peripheralPower >= 0) {
    const auto gpio = static_cast<gpio_num_t>(f.peripheralPower);
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    (void)gpio_set_level(gpio, 1);
  }

  if (f.panel.kind == PanelKind::CO5300_QSPI) {
    return bringUpUltra();
  }
  return true;
}

bool Buttons::begin(void) {
  const Facts &f = facts();
  configureInputPin(f.input.trackballLeft);
  configureInputPin(f.input.trackballRight);
  configureInputPin(f.input.trackballUp);
  configureInputPin(f.input.trackballDown);
  configureInputPin(f.input.trackballClick);
  configureInputPin(f.input.sideButton);
  configureInputPin(f.i2c.keyboardInterrupt);

  m_Levels = 0;
  m_Pending = 0;
  m_Releasing = 0;
  m_Held = 0;
  m_Samples = 0;

  if (f.i2c.keyboardAddress == 0) {
    return true;
  }
  if (!lgfx::i2c::init(f.i2c.port, f.i2c.sda, f.i2c.scl).has_value()) {
    return false;
  }
  // The controller needs time to bring its matrix up after the peripheral rail
  // comes on; the vendor's own master example waits 500 ms before its first
  // read and treats a NACK as "keyboard offline". One probe here reports the
  // same thing without stalling the UI.
  uint8_t pending = 0;
  return lgfx::i2c::transactionRead(f.i2c.port, f.i2c.keyboardAddress, &pending, 1,
                                    KEYBOARD_I2C_FREQ)
      .has_value();
}

void Buttons::sample(void) {
  const Facts &f = facts();
  uint8_t levels = 0;

  // Alternate the axis priority so a held diagonal reports both roles instead
  // of pinning one of them for as long as the ball stays in the corner.
  const Nav direction = decodeDirection(
      readPin(f.input.trackballLeft), readPin(f.input.trackballRight), readPin(f.input.trackballUp),
      readPin(f.input.trackballDown), (m_Samples & 1) == 0);
  if (direction == Nav::LEFT) {
    levels |= 0x01;
  } else if (direction == Nav::RIGHT) {
    levels |= 0x02;
  }
  if (readPin(f.input.trackballClick) || readPin(f.input.sideButton)) {
    levels |= 0x04;
  }

  if (f.i2c.keyboardAddress != 0) {
    // The controller raises its interrupt line when it has a byte, so the bus is
    // read on that edge and on a slow poll only. Reading every 5 ms sample cost
    // about 200 transactions a second for a device that reports at most one
    // byte per press, and it left the interrupt line unused.
    // readPin() is true for a low pin, and the controller's interrupt is active
    // low, so a true read here means "a key byte is waiting".
    const bool hasByte = f.i2c.keyboardInterrupt < 0 || readPin(f.i2c.keyboardInterrupt);
    const bool poll = (m_Samples % KEYBOARD_POLL_SAMPLES) == 0;
    uint8_t key = 0;
    if ((hasByte || poll)
        && lgfx::i2c::transactionRead(f.i2c.port, f.i2c.keyboardAddress, &key, 1, KEYBOARD_I2C_FREQ)
               .has_value()
        && (key != 0)) {
      // Queued, never latched for one sample: LVGL reads the input devices every
      // 33 ms and this sampler runs every 5 ms, so a key that lived for a
      // single sample would be lost about six times in seven.
      injectKey(key);
    }
  }

  m_Levels = levels;
  ++m_Samples;
}

}  // namespace LilyGo
}  // namespace Furble

#endif  // FURBLE_SIM || FURBLE_LILYGO_NO_DEVICE_IO
#endif  // FURBLE_LILYGO_BOARD
