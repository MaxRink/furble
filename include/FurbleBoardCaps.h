#ifndef FURBLE_BOARD_CAPS_H
#define FURBLE_BOARD_CAPS_H

/**
 * Hardware capabilities derived from the board identity macros.
 *
 * platformio.ini, the simulator and the host test targets set one board
 * identity macro such as FURBLE_M5STICKS3. Code that depends on what the
 * hardware has asks for the capability here instead of naming a board, so a
 * new board with the same part only needs a line in this file.
 *
 * Every macro is defined to 0 or 1. Test it with `#if`, never `#ifdef` or
 * `defined()`, and include this header directly in every file that uses one.
 * tools/check_coupling_ratchet.py enforces both rules, because a missing
 * include would silently evaluate the macro as 0.
 *
 * The settings enum and its switch tables keep the board macro: the settings
 * contract scanner checks each board-conditional setting against the host
 * test target that defines that board macro.
 */

// The M5PM1 PMIC: hardware watchdog, power button, status LED, IMU wake
// latch, download recovery lock, timed wake and the speaker amplifier enable.
#if defined(FURBLE_M5STICKS3)
#define FURBLE_HAS_M5PM1 1
#else
#define FURBLE_HAS_M5PM1 0
#endif

// The GPS burst holds a NO_LIGHT_SLEEP lock, which makes sleep while
// connected safe for the GPS UART. See plans/98 and FurbleBatterySaver.h.
#if defined(FURBLE_M5STICKS3)
#define FURBLE_HAS_GPS_BURST_LOCK 1
#else
#define FURBLE_HAS_GPS_BURST_LOCK 0
#endif

// A board image built for one LilyGO product.
//
// M5Unified does not detect these boards, so the profile row comes from the
// build macro (Board::compiledProfile()), the panel is Furble's own
// lgfx::LGFX_Device (include/FurbleDisplay.h) and the input devices are
// Furble's own (include/FurbleLilyGo.h). The LilyGO facts for the family live
// in include/FurbleLilyGo.h; a new LilyGO board adds a row there and one arm
// here. See plans/387-lilygo-family-port.md.
#if defined(FURBLE_LILYGO_TDECK) || defined(FURBLE_LILYGO_TWATCH_ULTRA)
#define FURBLE_LILYGO_BOARD 1
#else
#define FURBLE_LILYGO_BOARD 0
#endif

// The console runs on the ESP32-S3 USB Serial/JTAG port, not on UART0. Every
// LilyGO board here is an ESP32-S3 with the native USB port.
#if defined(FURBLE_M5STICKS3) || FURBLE_LILYGO_BOARD || defined(FURBLE_USB_CONSOLE)
#define FURBLE_HAS_USB_SERIAL_JTAG_CONSOLE 1
#else
#define FURBLE_HAS_USB_SERIAL_JTAG_CONSOLE 0
#endif

// An SD card slot. The StickC family has none.
#if defined(FURBLE_M5STICKC) || defined(FURBLE_M5STICKC_PLUS)
#define FURBLE_HAS_SD 0
#else
#define FURBLE_HAS_SD 1
#endif

// Software-readable VBUS. The Waveshare ESP32-S3-ETH exposes none, and neither
// LilyGO board here wires a charger status input to the SoC: the T-Deck's
// charger drives a charge LED and the T-Watch Ultra's AXP2101 reports charging
// over I2C, which this port does not read yet. A zero column keeps M5Unified's
// undriven judgement out of the battery display.
#if defined(FURBLE_WAVESHARE_S3_ETH) || FURBLE_LILYGO_BOARD
#define FURBLE_HAS_VBUS_SENSE 0
#else
#define FURBLE_HAS_VBUS_SENSE 1
#endif

// An internal I2C bus with devices M5Unified can identify. The Waveshare has
// none: the pins M5Unified would use for one are the camera header and W5500
// pins, so opening In_I2C there manufactures bus traffic on a pin that is not
// a bus. Neither LilyGO image opens M5Unified's In_I2C: the T-Deck keyboard and
// the T-Watch Ultra PMIC/expander sit on their own buses, driven here.
#if defined(FURBLE_WAVESHARE_S3_ETH) || FURBLE_LILYGO_BOARD
#define FURBLE_HAS_INTERNAL_I2C 0
#else
#define FURBLE_HAS_INTERNAL_I2C 1
#endif

// An AXP2101 whose rails have to be brought up before the panel answers, and
// an XL9555 on the same bus whose output enables the panel supply. This is
// board bring-up, not a power backend: no battery telemetry, no power-off.
#if defined(FURBLE_LILYGO_TWATCH_ULTRA)
#define FURBLE_HAS_LILYGO_PMIC 1
#else
#define FURBLE_HAS_LILYGO_PMIC 0
#endif

// Panel classes. At most one is 1. Headless profiles have none.
#if defined(FURBLE_M5STICKC)
#define FURBLE_PANEL_80X160 1
#else
#define FURBLE_PANEL_80X160 0
#endif

#if defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
#define FURBLE_PANEL_135X240 1
#else
#define FURBLE_PANEL_135X240 0
#endif

// A 320x240 landscape panel. The Core family reaches it through M5Unified and
// the T-Deck reaches it through its own lgfx device, but the UI layout tier is
// the same, which is why the T-Deck port reuses the densest existing layout.
#if defined(FURBLE_M5COREX) || defined(FURBLE_LILYGO_TDECK)
#define FURBLE_PANEL_320X240 1
#else
#define FURBLE_PANEL_320X240 0
#endif

// A 502x410 AMOLED. It is not narrow and it is not the 320x240 Core tier, so
// the UI takes its wide defaults; the tier exists so a later layout pass can
// address it without another board macro.
#if defined(FURBLE_LILYGO_TWATCH_ULTRA)
#define FURBLE_PANEL_502X410 1
#else
#define FURBLE_PANEL_502X410 0
#endif

// A portrait Stick panel, 80 or 135 pixels wide.
#define FURBLE_PANEL_NARROW (FURBLE_PANEL_80X160 || FURBLE_PANEL_135X240)

// Any panel with room for the wide layouts. At most one large class is 1.
#define FURBLE_PANEL_LARGE (FURBLE_PANEL_320X240 || FURBLE_PANEL_502X410)

#endif
