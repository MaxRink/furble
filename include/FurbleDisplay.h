#ifndef FURBLE_DISPLAY_H
#define FURBLE_DISPLAY_H

#include <cstdint>

#include "FurbleBoardCaps.h"

/*
 * M5GFX supplies `lgfx::LGFX_Device`, the type this seam hands out, but it is on
 * the include path of a firmware or simulator build only. A host test and the
 * static analyser both read this header and neither draws anything, so the
 * include is probed instead of assumed: where M5GFX is reachable the seam is
 * declared exactly as before, and where it is not the header is empty rather
 * than a hard error. The static analysis job runs clang-tidy on this file in
 * isolation, which is how the unconditional include broke it.
 */
#if defined(__has_include)
#if __has_include(<M5GFX.h>)
#define FURBLE_DISPLAY_HAS_M5GFX 1
#include <M5GFX.h>
#endif
#endif

#if defined(FURBLE_DISPLAY_HAS_M5GFX)

/**
 * The display the UI draws through.
 *
 * On every M5 board this is M5Unified's own display, which `M5.begin()`
 * configures from the detected board; the adapter defines `screen()` and every
 * other entry point here is a one-line forwarder, so the M5 images behave
 * exactly as they did before the seam existed.
 *
 * On a LilyGO board M5Unified detects nothing it can drive, so this is a device
 * built here from an M5GFX panel class plus the board's real wiring
 * (include/FurbleLilyGo.h). That is the panel configuration path the LilyGO
 * family shares: an ST7789 reached over SPI on the T-Deck, a CO5300 reached
 * over QSPI on the T-Watch Ultra. A third LilyGO board whose controller M5GFX
 * 0.2.27 already carries costs one facts row and no display code; a board whose
 * controller it does not carry (RM67162, SH8601, ST7796 outside what M5GFX
 * ships) needs a driver before it can have a row at all, which is why those
 * rows are absent rather than guessed.
 */
namespace Furble {
namespace Display {

#if FURBLE_LILYGO_BOARD || defined(FURBLE_LILYGO_TBEAM_SUPREME)

/** The device the UI draws through. Valid after begin(). */
lgfx::LGFX_Device &screen(void);

/**
 * Bring the board panel up.
 *
 * Returns false when the panel did not answer. Drawing then stays off the bus
 * rather than into a device that is not there, which is what the caller needs
 * to decide between a blank screen and a retry.
 */
bool begin(void);

/** True after a panel answered. */
bool ready(void);

/** Panel width in pixels, 0 before begin(). */
int32_t width(void);

/** Panel height in pixels, 0 before begin(). */
int32_t height(void);

#else
/*
 * The M5 side of the seam.
 *
 * `screen()` is defined by src/FurbleUI.cpp for an M5 image: that file is
 * compiled by every M5 display image and by the simulator, which is exactly the
 * set of M5 images that calls the seam. Everything else here is a plain
 * forwarder, so the seam added no coupling to this header: it removed 41
 * `M5.Display` uses from four files and added one.
 */
lgfx::LGFX_Device &screen(void);

inline bool begin(void) {
  return true;
}

inline bool ready(void) {
  return true;
}

inline int32_t width(void) {
  return screen().width();
}

inline int32_t height(void) {
  return screen().height();
}

/**
 * Panel geometry the T-Beam driver configures and the simulator models.
 */
constexpr int32_t TBEAM_PANEL_WIDTH = 128;
constexpr int32_t TBEAM_PANEL_HEIGHT = 64;

namespace Detail {

/**
 * The address the panel answered on, or 0 when neither answered.
 */
constexpr uint8_t choosePanelAddress(bool ack_primary, bool ack_secondary) {
  if (ack_secondary) {
    return 0x3D;
  }
  return ack_primary ? static_cast<uint8_t>(0x3C) : static_cast<uint8_t>(0);
}

/** SH1106 multiplex ratio for a panel of `height` rows. */
constexpr uint8_t sh1106MuxRatio(int32_t height) {
  return static_cast<uint8_t>(height - 1);
}

}  // namespace Detail

#endif

/**
 * Panel geometry the T-Beam driver configures and the simulator models.
 *
 * The SH1106 module fitted to the board is a 1.3 inch 128x64 part, so the
 * driver programs 64 commons and the simulator builds a window of the same
 * size. One constant per dimension keeps the two from drifting apart: the host
 * test asserts the multiplex ratio the driver sends is derived from the value
 * the simulator is built with.
 */
constexpr int32_t TBEAM_PANEL_WIDTH = 128;
constexpr int32_t TBEAM_PANEL_HEIGHT = 64;

namespace Detail {

/**
 * The address the panel answered on, or 0 when neither answered.
 *
 * The magnetometer shares the display bus and answers 0x3C on the variants that
 * strap it there, so when both addresses acknowledge, the panel is the one at
 * 0x3D: the board cannot carry two devices at one address, and the OLED strap
 * (R51 for 0x3D, R52 for 0x3C) is the only pairing that leaves 0x3C to the
 * magnetometer. A NACK on both means no panel, and begin() then refuses to
 * draw rather than driving an address it never saw acknowledged.
 */
constexpr uint8_t choosePanelAddress(bool ack_primary, bool ack_secondary) {
  if (ack_secondary) {
    return 0x3D;
  }
  return ack_primary ? static_cast<uint8_t>(0x3C) : static_cast<uint8_t>(0);
}

/**
 * SH1106 multiplex ratio for a panel of `height` rows.
 *
 * The datasheet expresses the ratio as rows - 1, so a 64-row module takes 0x3F.
 * M5GFX's `Panel_SH110x` hard-codes 0x7F for its 128-row default, which is why
 * the driver overrides the init list rather than taking the class default.
 */
constexpr uint8_t sh1106MuxRatio(int32_t height) {
  return static_cast<uint8_t>(height - 1);
}

}  // namespace Detail
}  // namespace Display
}  // namespace Furble

#endif

#endif  // FURBLE_DISPLAY_HAS_M5GFX
