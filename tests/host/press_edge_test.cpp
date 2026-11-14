#include <cassert>
#include <cstdio>

#include "FurblePressEdge.h"

using Furble::PressEdge;

int main() {
  constexpr uint8_t BUTTON = 0x02;
  constexpr uint8_t TOUCH = 0x08;
  PressEdge edge;

  // A held button reads pressed on every sample and counts once.
  assert(edge.sample(BUTTON, true));
  assert(!edge.sample(BUTTON, true));
  assert(!edge.sample(BUTTON, false));
  assert(edge.sample(BUTTON, true));
  assert(!edge.sample(BUTTON, false));

  // A touch-board tap drives the touch source and the synthesized button in
  // the same pass, in either order. It counts once (PR #454 review).
  assert(edge.sample(TOUCH, true));
  assert(!edge.sample(BUTTON, true));
  assert(!edge.sample(TOUCH, false));
  assert(!edge.sample(BUTTON, false));
  assert(edge.sample(BUTTON, true));
  assert(!edge.sample(TOUCH, true));
  assert(!edge.sample(BUTTON, false));
  assert(!edge.sample(TOUCH, false));

  // A source released without a sample, such as a handled long press, lets
  // the next press count.
  assert(edge.sample(BUTTON, true));
  edge.release(BUTTON);
  assert(edge.sample(BUTTON, true));

  std::puts("press-edge: PASS");
  return 0;
}
