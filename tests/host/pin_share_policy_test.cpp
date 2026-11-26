// Pins the shared LED/IR pin arbitration (plan 350).
//
// The Plus2 drives the red LED and the IR emitter from GPIO19. The pure state
// machine decides who owns the pin and which level survives a frame; the
// boundary target `boundary-shared-pin-*` covers the same rule against the
// production Feedback and IR drivers on the hardware doubles.

#include <cstdint>
#include <cstdio>

#include "FurblePinShare.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

}  // namespace

int main() {
  using Furble::PinShare::Arbiter;
  using Furble::PinShare::ledLevel;
  using Furble::PinShare::ledSharesIr;
  using Furble::PinShare::Owner;

  // Only the Plus2 row gives the LED and the emitter one pin. The StickC and
  // the StickC Plus keep G10 and G9 apart, a missing LED is -1, and a missing
  // emitter is -1 as well, so neither pairs up with the other.
  check(ledSharesIr(19, 19), "the Plus2 LED and IR pins are shared");
  check(!ledSharesIr(10, 9), "the StickC LED and IR pins are separate");
  check(!ledSharesIr(-1, 46), "a board without an LED has nothing to share");
  check(!ledSharesIr(10, -1), "a board without an emitter has nothing to share");
  check(!ledSharesIr(-1, -1), "two absent pins are not a shared pin");

  // The StickC family LED is active low, so on is 0 and off is 1.
  check(ledLevel(true, true) == 0, "an active-low LED is driven low when on");
  check(ledLevel(false, true) == 1, "an active-low LED is driven high when off");
  check(ledLevel(true, false) == 1, "an active-high LED is driven high when on");
  check(ledLevel(false, false) == 0, "an active-high LED is driven low when off");

  Arbiter arbiter;
  check(arbiter.owner() == Owner::NONE, "the arbiter starts with no owner");
  check(!arbiter.ledKnown(), "no LED level is remembered before Feedback asks");
  check(!arbiter.endIr(), "ending a frame without one is refused");

  // Feedback owns the pin until an IR frame asks for it.
  check(arbiter.requestLed(1), "the idle LED request may drive the pin");
  check(arbiter.ledKnown() && arbiter.ledLevel() == 1, "the idle level is remembered");
  check(arbiter.beginIr(), "an IR frame takes the pin");
  check(arbiter.owner() == Owner::IR, "the frame owns the pin");
  check(!arbiter.beginIr(), "a second frame cannot take the pin from the first");

  // A blink during the frame is recorded, not driven, and the frame hand-over
  // restores exactly that level rather than the level from before the frame.
  check(!arbiter.requestLed(0), "a blink during a frame does not drive the pin");
  check(arbiter.ledLevel() == 0, "the blink level is remembered during the frame");
  check(arbiter.endIr(), "the frame hands the pin back");
  check(arbiter.owner() == Owner::LED, "the LED owns the pin again");
  check(arbiter.ledLevel() == 0, "the level restored after the frame is the blink level");
  check(!arbiter.endIr(), "a second hand-over is refused");
  check(arbiter.requestLed(1), "the LED drives the pin again after the frame");

  // A frame can take a pin Feedback never drove. Nothing is remembered then,
  // which is what tells the glue to release the pad instead of inventing a
  // level for an unverified LED polarity.
  Arbiter untouched;
  check(untouched.beginIr(), "a frame takes an untouched pin");
  check(untouched.endIr(), "an untouched pin is handed back");
  check(!untouched.ledKnown(), "no level is invented for an untouched pin");

  if (failures != 0) {
    return 1;
  }
  std::printf("pin-share-policy: PASS\n");
  return 0;
}
