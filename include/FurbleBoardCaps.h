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

// The console runs on the ESP32-S3 USB Serial/JTAG port, not on UART0.
#if defined(FURBLE_M5STICKS3) || defined(FURBLE_USB_CONSOLE)
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

// Software-readable VBUS. The Waveshare ESP32-S3-ETH exposes none.
#if defined(FURBLE_WAVESHARE_S3_ETH)
#define FURBLE_HAS_VBUS_SENSE 0
#else
#define FURBLE_HAS_VBUS_SENSE 1
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

#if defined(FURBLE_M5COREX)
#define FURBLE_PANEL_320X240 1
#else
#define FURBLE_PANEL_320X240 0
#endif

// A portrait Stick panel, 80 or 135 pixels wide.
#define FURBLE_PANEL_NARROW (FURBLE_PANEL_80X160 || FURBLE_PANEL_135X240)

#endif
