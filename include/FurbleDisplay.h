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

#if FURBLE_LILYGO_BOARD

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

#endif

}  // namespace Display
}  // namespace Furble

#endif

#endif  // FURBLE_DISPLAY_HAS_M5GFX
