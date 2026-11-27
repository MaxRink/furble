// Pins the LilyGO family facts and the two pure input decoders.
//
// The facts are wiring: a wrong pin here is a port that drives another chip's
// bus. Every value is from a LilyGO document, with the URL and retrieval date
// in plans/387-lilygo-family-port.md, and the test states them from the vendor,
// never from the table it is checking. The decoders are the keyboard protocol,
// which the vendor's keyboard firmware source defines: one character per press,
// 0x0D for Enter, 0x08 for Backspace and 0x00 for nothing.

#include <cstring>
#include <iostream>
#include <string>

#include "FurbleLilyGo.h"

using Furble::LilyGo::BoardId;
using Furble::LilyGo::Nav;
using Furble::LilyGo::PanelKind;

namespace {

int g_Failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_Failures++;
  }
}

void checkTdeck(void) {
  const auto &f = Furble::LilyGo::factsFor(BoardId::TDECK);
  const std::string n = "T-Deck";
  check(f.id == BoardId::TDECK, n + " row id");
  check(std::strcmp(f.name, "T-Deck") == 0, n + " name");

  // LilyGoLib docs/hardware/lilygo-t-deck.md, "Pin Map": the display, MicroSD
  // and LoRa share SPI MOSI 41, MISO 38, SCK 40, and the ESP32-S3's general
  // purpose host is SPI2_HOST (1).
  check(f.spi.sck == 40 && f.spi.miso == 38 && f.spi.mosi == 41, n + " shared SPI pins");
  check(f.qspi.sck == -1 && f.qspi.io0 == -1, n + " no quad-SPI panel bus");
  check(f.spi.host == 1, n + " SPI2_HOST");
  check(f.spi.frequency == 40000000, n + " 40 MHz panel clock");

  // Display CS 12, DC 11, no reset pin, backlight 42; 2.8 inch ST7789 driven as
  // 320x240 landscape.
  check(f.panel.kind == PanelKind::ST7789_SPI, n + " ST7789 over SPI");
  check(f.panel.cs == 12 && f.panel.dc == 11 && f.panel.rst == -1 && f.panel.backlight == 42,
        n + " panel control pins");
  check(f.panel.width == 240 && f.panel.height == 320, n + " native 240x320");
  check(drivenWidth(f.panel) == 320 && drivenHeight(f.panel) == 240,
        n + " driven as 320x240 landscape");
  check(f.panel.offsetX == 0 && f.panel.offsetY == 0, n + " no address-window offset");
  check(f.panel.rotation == 1, n + " rotation 1 is the landscape one");
  check(f.panel.invert, n + " IPS panel is inverted");

  // I2C SDA 18, SCL 8; keyboard controller at 0x55 with its interrupt on 46;
  // no PMIC and no IO expander on this board.
  check(f.i2c.sda == 18 && f.i2c.scl == 8, n + " I2C pins");
  check(f.i2c.keyboardAddress == 0x55, n + " keyboard controller address");
  check(f.i2c.keyboardInterrupt == 46, n + " keyboard interrupt pin");
  check(f.i2c.pmicAddress == 0 && f.i2c.expanderAddress == 0, n + " no PMIC or expander");

  // Trackball right 2, up 3, left 1, down 15, click 0; no side button.
  check(f.input.trackballRight == 2 && f.input.trackballUp == 3, n + " trackball right/up");
  check(f.input.trackballLeft == 1 && f.input.trackballDown == 15, n + " trackball left/down");
  check(f.input.trackballClick == 0, n + " trackball center shares GPIO0");
  check(f.input.sideButton == -1, n + " has no side button");

  // MAX98357A: BCLK 7, WS 5, data out 6.
  check(f.speaker.bck == 7 && f.speaker.ws == 5 && f.speaker.dataOut == 6, n + " I2S speaker pins");

  // GPIO10 gates the peripherals and GPIO4 is the battery divider.
  check(f.peripheralPower == 10, n + " peripheral power gate");
  check(f.batteryAdc == 4, n + " battery ADC pin");
}

void checkUltra(void) {
  const auto &f = Furble::LilyGo::factsFor(BoardId::TWATCH_ULTRA);
  const std::string n = "T-Watch Ultra";
  check(f.id == BoardId::TWATCH_ULTRA, n + " row id");
  check(std::strcmp(f.name, "TWatch-U") == 0, n + " name");

  // LilyGoLib variants/lilygo_twatch_ultra/pins_arduino.h: the shared SPI bus
  // for SD, LoRa and NFC is SCK 35, MISO 33, MOSI 34.
  check(f.spi.sck == 35 && f.spi.miso == 33 && f.spi.mosi == 34, n + " shared SPI pins");
  // The panel bus is separate: SCK 40, D0 38, D1 39, D2 42, D3 45, from
  // LilyGoLib variants/lilygo_twatch_ultra/pins_arduino.h DISP_*.
  check(f.qspi.sck == 40 && f.qspi.io0 == 38 && f.qspi.io1 == 39 && f.qspi.io2 == 42
            && f.qspi.io3 == 45,
        n + " panel quad-SPI pins");
  check(f.spi.host == 1, n + " SPI2_HOST");

  // Panel: CO5300 over QSPI, CS 41, reset 37, no GPIO backlight, native 410x502.
  check(f.panel.kind == PanelKind::CO5300_QSPI, n + " CO5300 over QSPI");
  check(f.panel.cs == 41 && f.panel.rst == 37, n + " panel CS and reset");
  check(f.panel.dc == -1, n + " a QSPI panel has no D/C line");
  check(f.panel.backlight == -1, n + " brightness is a panel command");
  // The controller's own init programs CASET 22..431 and RASET 0..501, so the
  // native panel is 410 wide by 502 tall at a 22 column offset. A 502 wide fact
  // with that offset would ask for CASET 22..523 and put 92 columns of every
  // frame off the panel.
  check(f.panel.width == 410 && f.panel.height == 502, n + " native 410x502");
  check(f.panel.offsetX == 22, n + " the panel window starts at column 22");
  check(f.panel.offsetY == 0, n + " no row offset");
  check(drivenWidth(f.panel) == 502 && drivenHeight(f.panel) == 410,
        n + " driven as 502x410 landscape");
  check(f.panel.rotation == 1, n + " rotation 1 is the landscape one");
  // At rotation 1 M5GFX swaps the offsets, so the window counts stay in range:
  // 502 columns and 410 rows, starting 22 along the axis that carries the
  // offset. Both windows fit the controller's 432 by 502 address space.
  check(f.panel.rotation % 2 == 1 && f.panel.offsetX < 32, n + " offset fits the window");
  check(!f.panel.invert, n + " AMOLED is not inverted");

  // I2C SDA 3, SCL 2; AXP2101 at 0x34 and XL9555 at 0x20; no I2C keyboard.
  check(f.i2c.sda == 3 && f.i2c.scl == 2, n + " I2C pins");
  check(f.i2c.pmicAddress == 0x34, n + " AXP2101 address");
  check(f.i2c.expanderAddress == 0x20, n + " XL9555 address");
  check(f.i2c.keyboardAddress == 0, n + " no I2C key matrix");

  // No trackball; BOOT on GPIO0 is the only user switch.
  check(f.input.trackballLeft == -1 && f.input.trackballRight == -1 && f.input.trackballUp == -1
            && f.input.trackballDown == -1 && f.input.trackballClick == -1,
        n + " has no trackball");
  check(f.input.sideButton == 0, n + " side button is BOOT on GPIO0");

  // MAX98357A: BCLK 9, WS 10, data out 11.
  check(f.speaker.bck == 9 && f.speaker.ws == 10 && f.speaker.dataOut == 11,
        n + " I2S speaker pins");

  check(f.peripheralPower == -1, n + " no peripheral gate: the PMIC switches rails");
  check(f.batteryAdc == -1, n + " battery is the AXP2101's");
}

void checkDecoders(void) {
  // The vendor keyboard firmware sends one character per press, with Shift
  // already applied, and 0x00 when nothing is pending.
  check(Furble::LilyGo::decodeKey(0x0D) == Nav::SELECT, "Enter is the select key");
  check(Furble::LilyGo::decodeKey(0x08) == Nav::LEFT, "Backspace is the back key");
  check(Furble::LilyGo::decodeKey(0x0C) == Nav::LEFT, "Alt+C is the cancel key");
  check(Furble::LilyGo::decodeKey(0x00) == Nav::NONE, "an idle read is not a key");
  check(Furble::LilyGo::decodeKey('a') == Nav::NONE, "a letter has no navigation role");
  check(Furble::LilyGo::decodeKey(0x7F) == Nav::NONE, "an unknown byte has no role");

  // Active-low direction switches; up and down take the same roles as left and
  // right because every list in the UI is one-dimensional.
  check(Furble::LilyGo::decodeDirection(true, false, false, false) == Nav::LEFT,
        "left is the left role");
  check(Furble::LilyGo::decodeDirection(false, false, true, false) == Nav::LEFT,
        "up is the left role");
  check(Furble::LilyGo::decodeDirection(false, true, false, false) == Nav::RIGHT,
        "right is the right role");
  check(Furble::LilyGo::decodeDirection(false, false, false, true) == Nav::RIGHT,
        "down is the right role");
  check(Furble::LilyGo::decodeDirection(false, false, false, false) == Nav::NONE,
        "nothing pressed is no event");
  // A diagonal that holds two directions must reach both roles: with the
  // priority fixed one way the second direction would never be reported.
  check(Furble::LilyGo::decodeDirection(true, false, false, true, true) == Nav::LEFT,
        "diagonal left+down, horizontal first");
  check(Furble::LilyGo::decodeDirection(true, false, false, true, false) == Nav::RIGHT,
        "diagonal left+down, vertical first");
  check(Furble::LilyGo::decodeDirection(true, true, false, false) == Nav::LEFT,
        "a diagonal resolves to one direction");
}

void checkButtonLatch(void) {
  auto &buttons = Furble::LilyGo::Buttons::getInstance();
  buttons.begin();
  buttons.sample();
  check(!buttons.anyPressed(), "an idle sample has no press");

  // A key event is queued, not latched for one sample: the driver samples every
  // 5 ms and LVGL reads the input devices every 33 ms, so a press has to survive
  // several samples before it is read or it is lost.
  buttons.injectKey(0x0D);
  for (int i = 0; i < 5; ++i) {
    buttons.sample();
    check(buttons.anyPressed(), "a queued key survives a sample");
  }
  check(buttons.pressed(Nav::SELECT), "a keyboard Enter reads as select");
  check(!buttons.pressed(Nav::LEFT) && !buttons.pressed(Nav::RIGHT),
        "the Enter only reports the select role");
  check(!buttons.pressed(Nav::SELECT), "the read after the press is the release");
  check(!buttons.anyPressed(), "a consumed key is not held");

  buttons.injectKey(0x08);
  buttons.sample();
  check(buttons.pressed(Nav::LEFT), "a keyboard Backspace reads as left");
  check(!buttons.pressed(Nav::LEFT), "and releases on the next read");

  buttons.injectKey(0x7A);
  buttons.sample();
  check(!buttons.anyPressed(), "a character with no navigation role is dropped");

  buttons.injectNav(Nav::RIGHT);
  buttons.sample();
  check(buttons.pressed(Nav::RIGHT), "an injected right reads as right");
  check(!buttons.pressed(Nav::RIGHT), "an injected event releases on the next read");

  // A held role is a level: it reads pressed for as long as the simulator holds
  // it, which is what a scenario needs to reach the long-press handler.
  buttons.injectHold(Nav::LEFT, true);
  for (int i = 0; i < 3; ++i) {
    buttons.sample();
    check(buttons.pressed(Nav::LEFT), "a held role stays pressed across reads");
  }
  buttons.injectHold(Nav::LEFT, false);
  check(!buttons.pressed(Nav::LEFT), "a released hold reads released");
  check(!buttons.anyPressed(), "nothing is left pressed");
}

}  // namespace

int main() {
  checkTdeck();
  checkUltra();
  checkDecoders();
  checkButtonLatch();

  if (g_Failures == 0) {
    std::cout << "lilygo-family: PASS\n";
    return 0;
  }
  return 1;
}
