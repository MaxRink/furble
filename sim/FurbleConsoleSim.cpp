// Console entry points the simulator links but does not run.
//
// The simulator builds with FURBLE_CONSOLE so the UI-side request handlers in
// src/FurbleUI.cpp are compiled and exercised by scenarios. It does not build
// src/FurbleConsole.cpp: that file is the serial transport and the command
// parser, both of which are already driven end to end by the host suite
// (tests/host/console_commands_test.cpp) against the real ESP-IDF console API.
//
// What is left is the handful of entry points other firmware calls when the
// console is compiled in. A scenario drives requests through
// UI::sendRequest(), not through a serial line, so these have nothing to do.
#include "FurbleConsole.h"

namespace Furble {

void Console::init(void) {}

void Console::gpsRaw(const char *data, size_t length) {
  (void)data;
  (void)length;
}

void Console::gpsBinary(const uint8_t *data, size_t length) {
  (void)data;
  (void)length;
}

}  // namespace Furble
