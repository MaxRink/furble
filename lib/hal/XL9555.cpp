#include "XL9555.h"

namespace Furble {
namespace HAL {

namespace {
constexpr uint8_t REG_INPUT = 0x00;
constexpr uint8_t REG_OUTPUT = 0x02;
constexpr uint8_t REG_CONFIG = 0x06;

uint8_t reg(uint8_t base, uint8_t pin) {
  return static_cast<uint8_t>(base + (pin >> 3));
}

uint8_t mask(uint8_t pin) {
  return static_cast<uint8_t>(1U << (pin & 0x07U));
}
}  // namespace

bool XL9555::setInput(uint8_t pin) {
  return (pin < PIN_COUNT) && m_Bus.update8(reg(REG_CONFIG, pin), mask(pin), mask(pin));
}

bool XL9555::setOutput(uint8_t pin, bool level) {
  return write(pin, level) && m_Bus.update8(reg(REG_CONFIG, pin), mask(pin), 0);
}

bool XL9555::write(uint8_t pin, bool level) {
  return (pin < PIN_COUNT) && m_Bus.update8(reg(REG_OUTPUT, pin), mask(pin), level ? mask(pin) : 0);
}

bool XL9555::read(uint8_t pin, bool &level) {
  uint8_t value = 0;
  if ((pin >= PIN_COUNT) || !m_Bus.read8(reg(REG_INPUT, pin), value)) {
    return false;
  }
  level = (value & mask(pin)) != 0;
  return true;
}

}  // namespace HAL
}  // namespace Furble
