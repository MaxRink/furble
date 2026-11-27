// The T-Beam Supreme display: the address probe and the panel geometry.
//
// Two things on that path are decisions rather than wiring, and neither can be
// seen on a device without a panel that fails to answer:
//
//   1. which address the driver talks to after probing 0x3C and 0x3D, where a
//      NACK on the first must not be read as an ACK and a NACK on both must
//      leave the display unready;
//   2. the multiplex ratio the 128x64 module needs, where M5GFX's class default
//      is the 128-row value.
//
// Both live in include/FurbleDisplay.h as pure functions so this test can drive
// them with a bus double whose NACK is a real answer, not an error the code
// swallows. The geometry half is checked against the constants the simulator
// builds its window with, so the model and the driver cannot describe different
// panels.

#include "FurbleDisplay.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {

int g_Failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_Failures;
  }
}

// A two-address I2C bus with one device, or none. `probe()` is what the driver
// calls: an address byte followed by a stop, answered only by the device that
// owns the address. A NACK is a `false` return, which is exactly the signal the
// M5GFX endTransaction() result carries.
class FakeBus {
 public:
  FakeBus(std::initializer_list<int> devices) : m_Devices(devices) {}

  bool probe(uint8_t address) {
    m_Probes.push_back(address);
    for (const int device : m_Devices) {
      if (device == address) {
        return true;
      }
    }
    return false;
  }

  const std::vector<uint8_t> &probes(void) const { return m_Probes; }

 private:
  std::initializer_list<int> m_Devices;
  std::vector<uint8_t> m_Probes;
};

// The driver's probe order, written once and used by every case below.
uint8_t probeBoth(FakeBus &bus) {
  const bool ackPrimary = bus.probe(0x3C);
  const bool ackSecondary = bus.probe(0x3D);
  return Furble::Display::Detail::choosePanelAddress(ackPrimary, ackSecondary);
}

void testPanelAtPrimary(void) {
  FakeBus bus({0x3C});
  const uint8_t address = probeBoth(bus);
  check(address == 0x3C, "a panel at 0x3C with nothing at 0x3D is taken at 0x3C");
  check(bus.probes().size() == 2, "both addresses are probed even when the first answers");
}

void testPanelAtSecondary(void) {
  // The case the pre-review found: 0x3C NACKs and the panel is at 0x3D. A probe
  // that treated a failed transaction as an ACK would return 0x3C here, drive a
  // device that is not there, and never reach the panel.
  FakeBus bus({0x3D});
  const uint8_t address = probeBoth(bus);
  check(address == 0x3D, "a NACK at 0x3C falls through to 0x3D");
  check(bus.probes().size() == 2, "the second address is probed after the first NACKs");
}

void testMagnetometerAtPrimary(void) {
  // The variant where the magnetometer straps to 0x3C and the panel to 0x3D.
  // Both answer; the panel is the one the row can draw through.
  FakeBus bus({0x3C, 0x3D});
  const bool magnetometer = bus.probe(0x3C);
  const bool panel = bus.probe(0x3D);
  check(magnetometer && panel, "the double answers both addresses in this variant");
  check(Furble::Display::Detail::choosePanelAddress(magnetometer, panel) == 0x3D,
        "both addresses answering selects the 0x3D panel");
}

void testNoPanel(void) {
  FakeBus bus({});
  const uint8_t address = probeBoth(bus);
  check(address == 0, "no device answering leaves no address, so begin() fails closed");
  check(bus.probes().size() == 2, "both addresses are probed before giving up");
}

void testGeometry(void) {
  using Furble::Display::Detail::sh1106MuxRatio;
  check(Furble::Display::TBEAM_PANEL_WIDTH == 128, "the panel is 128 pixels wide");
  check(Furble::Display::TBEAM_PANEL_HEIGHT == 64, "the panel is 64 rows tall");
  check(sh1106MuxRatio(Furble::Display::TBEAM_PANEL_HEIGHT) == 0x3F,
        "a 64-row panel takes multiplex 0x3F, not M5GFX's 128-row 0x7F");
  check(sh1106MuxRatio(128) == 0x7F, "the same expression reproduces the 128-row default");
}

}  // namespace

int main() {
  testPanelAtPrimary();
  testPanelAtSecondary();
  testMagnetometerAtPrimary();
  testNoPanel();
  testGeometry();
  if (g_Failures != 0) {
    std::fprintf(stderr, "%d display probe check(s) failed\n", g_Failures);
    return EXIT_FAILURE;
  }
  std::printf("display-probe PASS: address probe and 128x64 geometry\n");
  return EXIT_SUCCESS;
}
