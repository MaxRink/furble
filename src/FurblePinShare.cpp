#include <driver/gpio.h>

#include <mutex>

#include "FurbleBoard.h"
#include "FurblePinShare.h"

/*
 * One pin, two drivers (see include/FurblePinShare.h).
 *
 * Feedback runs on the UI task and IR on its own task, so the state machine and
 * the pad writes are serialised by one short mutex. It is never held across an
 * RMT transfer: IR calls beginIr(), transfers, then calls endIr().
 */
namespace Furble {
namespace PinShare {

namespace {
std::mutex g_Mutex;
Arbiter g_Arbiter;

/** The LED pin of the running board, or -1 when it has none. */
int ledPin(void) {
  return Board::profile().ledPin;
}
}  // namespace

bool requestLed(uint8_t level) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  const int pin = ledPin();
  if (pin < 0) {
    return false;
  }
  if (!g_Arbiter.requestLed(level)) {
    // An IR frame owns the pin. The level is remembered and restored when the
    // frame ends.
    return false;
  }
  (void)gpio_set_level(static_cast<gpio_num_t>(pin), level);
  return true;
}

bool beginIr(int ir_pin) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!ledSharesIr(ledPin(), ir_pin)) {
    return false;
  }
  return g_Arbiter.beginIr();
}

bool endIr(void) {
  std::lock_guard<std::mutex> lock(g_Mutex);
  if (!g_Arbiter.endIr()) {
    return false;
  }

  const int pin = ledPin();
  if (pin < 0) {
    return false;
  }

  if (!g_Arbiter.ledKnown()) {
    // Feedback never asked for a level on this boot, so do not invent one: on
    // the Plus2 the pin drives an NPN stage that keys the LED and the emitter
    // together. Release the pad as an input, which is the state the boot path
    // leaves it in when light feedback is off.
    (void)gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
    return true;
  }

  // rmt_disable() leaves the pad muxed to the RMT channel signal, so a GPIO
  // write alone would not reach it. Set the level first, then move the pad back
  // to the plain GPIO path: gpio_set_direction() reaches the internal
  // gpio_output_enable(), which re-selects the default output signal
  // (esp-idf, components/esp_driver_gpio/src/gpio.c).
  (void)gpio_set_level(static_cast<gpio_num_t>(pin), g_Arbiter.ledLevel());
  (void)gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  return true;
}

}  // namespace PinShare
}  // namespace Furble
