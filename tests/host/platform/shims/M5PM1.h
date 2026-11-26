#ifndef FURBLE_PLATFORM_TEST_M5PM1_H
#define FURBLE_PLATFORM_TEST_M5PM1_H

// The one M5PM1 model, shared with the simulator and the pmic-irq target. It is
// reached by path rather than by an include directory because this target also
// needs the settings stubs, whose M5PM1.h is an empty placeholder, and the two
// cannot share an include order. Never fork the model here.
#include "../../../sim/shim/M5PM1.h"

#endif  // FURBLE_PLATFORM_TEST_M5PM1_H
