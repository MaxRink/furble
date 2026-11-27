#ifndef FURBLE_LILYGO_H
#define FURBLE_LILYGO_H

#include <cstdint>

/**
 * The LilyGO family: the facts every LilyGO board in this port shares, and the
 * seam its input devices and board bring-up go through.
 *
 * Why this exists. M5Unified knows no LilyGO board: its per-board pin table
 * describes M5 hardware, so a LilyGO image that let M5Unified choose would read
 * pins that are not there. Each LilyGO image therefore pins M5GFX at a
 * display-less M5 name only to suppress the autodetect probes
 * (`post:patches/m5gfx_board.py`), and everything board-specific comes from
 * here: the buses, the panel wiring, the input devices and the rail bring-up.
 *
 * What a third board costs. One row in this table, one arm in
 * `FURBLE_LILYGO_BOARD`'s friends in FurbleBoardCaps.h, one profile row in
 * src/FurbleBoard.cpp, one environment, and a simulator row. The panel path
 * already covers both controllers the family uses (`PanelKind`), so a board
 * with an ST7789 or an CO5300 needs no new display code at all. A board with a
 * controller M5GFX 0.2.27 does not carry does need a driver first: the family
 * plan records which LilyGO products that is true for, and they are
 * deliberately absent from this table rather than guessed at.
 *
 * Every value below is cited to a LilyGO document in
 * plans/387-lilygo-family-port.md, which also lists what the vendor does not
 * settle. Nothing here is inferred from a similar board.
 */
namespace Furble {
namespace LilyGo {

/** The LilyGO products this port has a fact row for. */
enum class BoardId : uint8_t {
  TDECK,
  TWATCH_ULTRA,
};

/** The panel controller, which is also the display code path. */
enum class PanelKind : uint8_t {
  ST7789_SPI,   // M5GFX lgfx::Panel_ST7789 over SPI
  CO5300_QSPI,  // M5GFX lgfx::Panel_CO5300 over QSPI
};

/** One SPI bus: the pins and the ESP SPI host the port drives it on. */
struct SpiBus {
  int8_t sck;
  int8_t miso;
  int8_t mosi;
  // ESP-IDF host id, not an index: 2 is SPI2_HOST, which is the only general
  // purpose host on the ESP32-S3 (SPI0/SPI1 are the flash and PSRAM buses).
  uint8_t host;
  uint32_t frequency;
};

/** The quad-SPI panel bus: one clock and four data lines, no MISO/MOSI pair. */
struct QspiBus {
  int8_t sck;
  int8_t io0;
  int8_t io1;
  int8_t io2;
  int8_t io3;
};

/**
 * The panel's wiring and its native geometry.
 *
 * `width` and `height` are the panel's own size at rotation 0, which is the
 * size the controller's address window is programmed in, and `offsetX`/`offsetY`
 * are that window's offsets. The port then drives the panel at `rotation`, and
 * M5GFX swaps both the size and the offsets for an odd rotation, so the size the
 * UI draws in is drivenWidth()/drivenHeight(). Keeping the native numbers here
 * is what makes the driver's column window checkable: the CO5300's own init
 * programs columns 22 to 431, which is a 410 column panel at offset 22, and a
 * 502 wide fact with that offset would put 92 columns of every frame off it.
 */
struct PanelWiring {
  PanelKind kind;
  int8_t cs;
  int8_t dc;  // -1 for a QSPI panel, which has no D/C line
  int8_t rst;
  int8_t backlight;  // -1 when the panel has no GPIO backlight
  int16_t width;
  int16_t height;
  uint16_t offsetX;
  uint16_t offsetY;
  uint8_t rotation;
  bool invert;
};

/** The width the UI draws in: the native width, swapped for an odd rotation. */
constexpr int16_t drivenWidth(const PanelWiring &panel) {
  return (panel.rotation & 1) != 0 ? panel.height : panel.width;
}

/** The height the UI draws in: the native height, swapped for an odd rotation. */
constexpr int16_t drivenHeight(const PanelWiring &panel) {
  return (panel.rotation & 1) != 0 ? panel.width : panel.height;
}

/** The board I2C bus and the devices this port talks to on it. */
struct I2cWiring {
  int8_t sda;
  int8_t scl;
  uint8_t port;
  uint8_t keyboardAddress;   // 0 when the board has no I2C key matrix
  int8_t keyboardInterrupt;  // -1 when none
  uint8_t pmicAddress;       // 0 when the board has no PMIC
  uint8_t expanderAddress;   // 0 when the board has no IO expander
};

/**
 * The navigation hardware.
 *
 * A `-1` pin means the board does not have that switch. The T-Watch Ultra has
 * no trackball, so every trackball pin is -1 there.
 */
struct InputWiring {
  int8_t trackballRight;
  int8_t trackballUp;
  int8_t trackballLeft;
  int8_t trackballDown;
  int8_t trackballClick;
  int8_t sideButton;
};

/**
 * The I2S speaker the board wires.
 *
 * Both boards in this port use a MAX98357A class-D amplifier with no enable
 * pin, so the amplifier is always powered and the fields describe the I2S
 * clocks only. A `-1` data pin means the board has no speaker.
 */
struct SpeakerWiring {
  int8_t bck;
  int8_t ws;
  int8_t dataOut;
  uint8_t port;
};

/** Everything one LilyGO board's bring-up needs. */
struct Facts {
  BoardId id;
  const char *name;
  SpiBus spi;
  /** The quad-SPI bus a CO5300 panel hangs from; all -1 on an SPI panel. */
  QspiBus qspi;
  PanelWiring panel;
  I2cWiring i2c;
  InputWiring input;
  SpeakerWiring speaker;
  /**
   * Driven high before any peripheral is used.
   *
   * The T-Deck gates its keyboard, panel, SD and radio supply with GPIO10 and
   * the vendor says peripherals may only be used while it is high on battery
   * power. -1 on a board without the gate.
   */
  int8_t peripheralPower;
  /** Battery voltage divider input, -1 when the board has no ADC divider. */
  int8_t batteryAdc;
};

/** The fact row for one board. Valid for every BoardId. */
const Facts &factsFor(BoardId id);

/** The fact row of the board this image is built for. */
const Facts &facts(void);

/** One navigation event, in the three roles the UI's input devices use. */
enum class Nav : uint8_t {
  NONE,
  LEFT,
  RIGHT,
  SELECT,
};

/**
 * Decode one byte from the T-Deck keyboard controller.
 *
 * The ESP32-C3 keyboard firmware sends one character per key press and 0x00
 * when nothing is pending. Enter is 0x0D and Backspace is 0x08; Alt+C sends
 * 0x0C. Every other byte is the key's character, with Shift already applied,
 * and has no navigation meaning in a UI with no text entry: it decodes to
 * NONE. The key map has no arrow keys, so the keyboard cannot move focus; see
 * plans/387.
 */
Nav decodeKey(uint8_t byte);

/**
 * Decode the trackball's four direction switches.
 *
 * The switches are active-low and read as levels, so a held direction repeats
 * through LVGL's long-press repeat. Up and Down take the same roles as Left and
 * Right because the UI's rows are one-dimensional.
 */
Nav decodeDirection(bool left, bool right, bool up, bool down);

/**
 * The same decode with the axis priority selectable.
 *
 * The three-role surface folds up into LEFT and down into RIGHT, so a diagonal
 * that holds, say, left and down matches both roles at once. Testing the
 * horizontal pair first every time would report LEFT for as long as the ball
 * sits in the corner and never report RIGHT, which starves half the diagonal.
 * The sampler flips `horizontalFirst` on alternate samples, so a held diagonal
 * alternates between the two roles and neither is starved.
 */
Nav decodeDirection(bool left, bool right, bool up, bool down, bool horizontalFirst);

/**
 * The board's input devices, sampled once per UI cycle.
 *
 * The three roles are the ones the UI already has: LEFT and RIGHT move the
 * focused row, SELECT activates it. On the T-Deck the trackball drives all
 * three and the keyboard's Enter and Backspace drive SELECT and LEFT; on the
 * T-Watch Ultra the side button drives SELECT. A keyboard key is a one-shot
 * event, so it is latched here and consumed by the next sample.
 */
class Buttons {
 public:
  static Buttons &getInstance();

  /** Configure the pins and the keyboard bus. Returns false on a bus error. */
  bool begin(void);

  /** Read the hardware into this cycle's level state and queue key events. */
  void sample(void);

  /**
   * True when `role` reads as pressed.
   *
   * The level devices (the trackball, the side button) answer directly. A key
   * event is a one-shot: the first call that sees it reports pressed and arms a
   * release, and the next call for that role reports released. That keeps a
   * press alive until LVGL actually reads the input device, which polls at
   * about 33 ms against this driver's 5 ms sample, so a press cannot be
   * swallowed between two samples. Not const: a read consumes the event.
   */
  bool pressed(Nav role);

  /** True when any role is pressed or a key event is still queued. */
  bool anyPressed(void) const;

  /** Number of latched samples, for the host test. */
  uint32_t samples(void) const { return m_Samples; }

  /** Test and simulator seam: feed one keyboard byte without a bus. */
  void injectKey(uint8_t byte);

  /** Test and simulator seam: feed one navigation event without a bus. */
  void injectNav(Nav nav);

  /**
   * Test and simulator seam: hold or release a navigation role.
   *
   * A keyboard key is momentary, so a scenario that wants a held key cannot get
   * one by injecting a byte. This asserts the role as a level for as long as the
   * caller keeps it down, through the same pressed() seam the read callbacks
   * use, so a hold really does reach the long-press handler. The firmware never
   * calls it.
   */
  void injectHold(Nav role, bool held);

 private:
  Buttons(void) = default;

  uint8_t m_Levels = 0;     // level devices, one bit per Nav role
  uint8_t m_Pending = 0;    // key events not yet read by LVGL
  uint8_t m_Releasing = 0;  // key events whose press was read, release next
  uint8_t m_Held = 0;       // simulator-only held roles
  uint32_t m_Samples = 0;
};

/**
 * Bring the board's power rails and peripheral gates up.
 *
 * The T-Deck only needs its peripheral gate. The T-Watch Ultra needs the
 * AXP2101 rails its panel, SD, speaker and GNSS hang from, and the XL9555
 * output that enables the panel supply, before the panel can answer. Returns
 * false when a device the board needs did not answer; the caller keeps the
 * panel off rather than drawing into a dark screen.
 */
bool powerOn(void);

}  // namespace LilyGo
}  // namespace Furble

#endif
