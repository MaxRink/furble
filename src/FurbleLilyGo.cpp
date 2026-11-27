#include "FurbleLilyGo.h"

#include "FurbleBoardCaps.h"

/*
 * The LilyGO family table.
 *
 * Every row is a read of a LilyGO document, never a copy from a similar board.
 * plans/387-lilygo-family-port.md carries the URL and retrieval date for each
 * column and lists the facts the vendor does not settle, which are marked
 * UNVERIFIED there rather than filled in here. Two products of the family are
 * deliberately absent: the T-LoRa Pager and the T4-S3 whose panels M5GFX
 * 0.2.27 cannot drive, and the T-Display S3 and T4-S3, which are bare boards
 * with no cell. An absent row is cheaper than a guessed one.
 *
 * SPI host numbers are ESP-IDF `spi_host_device_t` values: the ESP32-S3's
 * general purpose SPI is SPI2_HOST, which is 1. SPI0 and SPI1 are the flash and
 * PSRAM buses and are not available to a driver.
 */
namespace Furble {
namespace LilyGo {
namespace {

// LilyGO T-Deck and T-Deck Plus (same mainboard GPIO map):
// - LilyGoLib docs/hardware/lilygo-t-deck.md, "Pin Map", "I2C Device
//   Addresses", "Power Notes" and "Electrical Specifications".
// - LilyGO T-Deck wiki page, "Pin Mapping" and "Product Parameters".
// - Xinyuan-LilyGO/T-Deck examples/UnitTest/utilities.h (BOARD_* pins) and
//   examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino (keyboard protocol).
// The Plus difference is the GNSS, which takes the Grove UART pins 43 and 44;
// the base board leaves them on the Grove connector.
constexpr Facts TDECK_FACTS = {
    BoardId::TDECK,
    "T-Deck",
    // Display, MicroSD and the optional SX1262 share this one bus.
    {40, 38, 41, 1, 40000000},
    // No quad-SPI bus: the display is an ordinary SPI panel.
    {-1, -1, -1, -1, -1},
    // ST7789, 2.8 inch, native 240x320 at rotation 0, driven at rotation 1 so
    // the UI draws 320x240. No reset line: the panel resets with the peripheral
    // rail.
    {PanelKind::ST7789_SPI, 12, 11, -1, 42, 240, 320, 0, 0, 1, true},
    // Touch, keyboard and the ES7210 microphone codec share this bus.
    {18, 8, 0, 0x55, 46, 0, 0},
    // Trackball: four active-low direction switches and a center switch that
    // is also the ESP32-S3 BOOT strapping pin.
    {2, 3, 1, 15, 0, -1},
    // MAX98357A: BCLK 7, WS 5, data out 6 (LilyGoLib I2S_SCK/WS/SDOUT).
    {7, 5, 6, 1},
    10, // GPIO10 gates the peripherals and must be high on battery power
    4, // battery voltage through a 2:1 divider
};

// LilyGO T-Watch Ultra:
// - LilyGoLib docs/hardware/lilygo-t-watch-ultra.md, "Pin Map", "I2C Device
//   Addresses", "Power Management Channels" and "Electrical Specifications".
// - LilyGoLib variants/lilygo_twatch_ultra/pins_arduino.h (DISP_*, TP_*,
//   LORA_*, SD_CS, I2S_*, TX/RX/GPS_PPS, EXPANDS_*).
// - LilyGO T-Watch Ultra schematic V1.0 dated 2025-07-24 (MIA-M10Q, PPS).
constexpr Facts TWATCH_ULTRA_FACTS = {
    BoardId::TWATCH_ULTRA,
    "TWatch-U",
    // MicroSD, LoRa and NFC share this bus; the panel is on its own QSPI pins.
    {35, 33, 34, 1, 40000000},
    // Panel quad-SPI: SCK 40, D0 38, D1 39, D2 42, D3 45 (LilyGoLib
    // pins_arduino.h DISP_SCLK/DISP_SDIO/DISP_SDIO_1/DISP_SDIO_2/DISP_SDIO_3).
    {40, 38, 39, 42, 45},
    // CO5300 AMOLED, 2.06 inch, native 410x502 at rotation 0. The controller's
    // own init programs its column window as 22 to 431 (410 columns) and its
    // row window as 0 to 501, so the panel is 410 wide with a 22 column offset
    // and 502 tall with none. The port drives it at rotation 1, where M5GFX
    // writes CASET 0 to 501 and RASET 22 to 431, and the UI draws 502x410.
    {PanelKind::CO5300_QSPI, 41, -1, 37, -1, 410, 502, 22, 0, 1, false},
    // AXP2101, XL9555, BHI260AP, PCF85063A, DRV2605 and the CST9217 touch
    // controller share this bus.
    {3, 2, 0, 0, -1, 0x34, 0x20},
    // No trackball. The BOOT button on GPIO0 is the only user switch; the
    // POWER key belongs to the AXP2101.
    {-1, -1, -1, -1, -1, 0},
    // MAX98357A: BCLK 9, WS 10, data out 11 (LilyGoLib I2S_BCLK/WCLK/DOUT).
    {9, 10, 11, 1},
    -1, // no peripheral gate: the PMIC rails are switched instead
    -1, // battery is the AXP2101's, and this port does not read it yet
};

}  // namespace

const Facts &factsFor(BoardId id) {
  switch (id) {
    case BoardId::TWATCH_ULTRA:
      return TWATCH_ULTRA_FACTS;
    case BoardId::TDECK:
      break;
  }
  return TDECK_FACTS;
}

const Facts &facts(void) {
#if defined(FURBLE_LILYGO_TWATCH_ULTRA)
  return factsFor(BoardId::TWATCH_ULTRA);
#else
  return factsFor(BoardId::TDECK);
#endif
}

Nav decodeKey(uint8_t byte) {
  switch (byte) {
    case 0x0D:  // Enter, firmware key (3,3)
      return Nav::SELECT;
    case 0x08:  // Backspace, firmware key (4,3); the UI's back action
    case 0x0C:  // Alt+C, which the firmware sends as a "cancel" byte
      return Nav::LEFT;
    default:
      break;
  }
  // Every other byte is a character. There is no text entry in the UI, so a
  // character has no navigation role; it is dropped rather than mapped onto a
  // direction the key map does not have.
  return Nav::NONE;
}

Nav decodeDirection(bool left, bool right, bool up, bool down, bool horizontalFirst) {
  if (horizontalFirst) {
    if (left || up) {
      return Nav::LEFT;
    }
    if (right || down) {
      return Nav::RIGHT;
    }
  } else {
    if (right || down) {
      return Nav::RIGHT;
    }
    if (left || up) {
      return Nav::LEFT;
    }
  }
  return Nav::NONE;
}

Nav decodeDirection(bool left, bool right, bool up, bool down) {
  return decodeDirection(left, right, up, down, true);
}

Buttons &Buttons::getInstance() {
  static Buttons instance;
  return instance;
}

void Buttons::injectNav(Nav nav) {
  switch (nav) {
    case Nav::LEFT:
      m_Pending |= 0x01;
      break;
    case Nav::RIGHT:
      m_Pending |= 0x02;
      break;
    case Nav::SELECT:
      m_Pending |= 0x04;
      break;
    case Nav::NONE:
      break;
  }
}

void Buttons::injectKey(uint8_t byte) {
  injectNav(decodeKey(byte));
}

void Buttons::injectHold(Nav role, bool held) {
  uint8_t bit = 0;
  switch (role) {
    case Nav::LEFT:
      bit = 0x01;
      break;
    case Nav::RIGHT:
      bit = 0x02;
      break;
    case Nav::SELECT:
      bit = 0x04;
      break;
    case Nav::NONE:
      return;
  }
  if (held) {
    m_Held |= bit;
  } else {
    m_Held &= static_cast<uint8_t>(~bit);
  }
}

bool Buttons::pressed(Nav role) {
  uint8_t bit = 0;
  switch (role) {
    case Nav::LEFT:
      bit = 0x01;
      break;
    case Nav::RIGHT:
      bit = 0x02;
      break;
    case Nav::SELECT:
      bit = 0x04;
      break;
    case Nav::NONE:
      return false;
  }
  if ((m_Held & bit) != 0) {
    return true;
  }
  if ((m_Releasing & bit) != 0) {
    m_Releasing &= static_cast<uint8_t>(~bit);
    return false;
  }
  if ((m_Pending & bit) != 0) {
    m_Pending &= static_cast<uint8_t>(~bit);
    m_Releasing |= bit;
    return true;
  }
  return (m_Levels & bit) != 0;
}

bool Buttons::anyPressed(void) const {
  return (m_Levels | m_Pending | m_Held) != 0;
}

}  // namespace LilyGo
}  // namespace Furble
