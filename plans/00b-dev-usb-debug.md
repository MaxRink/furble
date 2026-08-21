# PR00: developer USB debug environments

## Goal

Add optional PlatformIO environments that make on-device debugging easy: JTAG
debugging over the ESP32-S3 built-in USB-Serial/JTAG on the M5StickS3, and
verbose log builds on all boards. No change to the shipping firmware.

## Scope

In scope:

- New `-debug` environment variants for all five boards.
- `build_type = debug` and verbose `ESP_LOG` level in those variants only.
- `debug_tool = esp-builtin` for the S3 variant.
- A short section in the README or the wiki pointing at the new environments.

Out of scope:

- Any change to the five shipping environments.
- Any change to the committed `sdkconfig.*` files.
- Runtime log level UI. That belongs to PR05.

## Files to change

- `platformio.ini:1-5`, the `[furble]` section holding shared `build_flags`. New
  variants inherit from it.
- `platformio.ini:7-21`, the `[env]` section. Shared `platform`,
  `platform_packages`, `board_build.f_cpu`, `upload_protocol = esptool`,
  `monitor_speed = 115200`, `lib_deps` and `extra_scripts` are inherited by every
  environment including the new ones.
- `platformio.ini:23-41`, the five board environments. Each new variant extends
  one of these.
- `README.md:94-106`, the PlatformIO build section. Add the debug environment
  names and the monitor command.

No C or C++ source changes.

## New settings

None. This PR is tooling only.

## Menu placement

None.

## Implementation notes

### Environment layout

Add one variant per board, named `<board>-debug`, each with
`extends = env:<board>`. Every variant sets:

- `build_type = debug`, which drops the optimisation level and keeps symbols.
- `build_flags = ${env:<board>.build_flags} -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` so
  `ESP_LOGD` and `ESP_LOGV` in furble sources compile in. Note this project uses
  `framework = espidf` (`platformio.ini:14`), so the Arduino style
  `CORE_DEBUG_LEVEL` flag does nothing here. Use `LOG_LOCAL_LEVEL`.

`CONFIG_LOG_DEFAULT_LEVEL=3` is baked into the committed sdkconfig files
(`sdkconfig.m5stick-s3:1759`, `sdkconfig.m5stick-c-plus:1551`). Raising it means
editing the sdkconfig, which would change the shipping build. Prefer
`LOG_LOCAL_LEVEL` per translation unit plus `esp_log_level_set()` at runtime, and
leave the committed sdkconfig files untouched. `esp_log_level_set` cannot exceed
`CONFIG_LOG_MAXIMUM_LEVEL`, so confirm that ceiling before relying on runtime
level changes; if it blocks verbose output, document the one line sdkconfig edit
in the README instead of committing it.

### S3 JTAG

The M5StickS3 is an ESP32-S3 with the USB-Serial/JTAG peripheral present:
`sdkconfig.m5stick-s3:27` has `CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED=y` and
`sdkconfig.m5stick-s3:1134` has `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y`. The
`m5stick-s3` environment already targets board `esp32-s3-devkitc-1`
(`platformio.ini:40`), and PlatformIO lists that board as having an on-board
debug probe with `esp-builtin` support. So the S3 debug variant only needs:

```
debug_tool = esp-builtin
debug_init_break = tbreak app_main
```

Leave `upload_protocol` inherited as `esptool`. Switching upload to
`esp-builtin` is optional and can be documented rather than set.

The USB-Serial/JTAG peripheral uses GPIO19 and GPIO20 on the S3. Confirm the
StickS3 exposes those pins to the USB-C port and that nothing else drives them
before claiming JTAG works. This is a one time check on the device.

### Console on the S3

`sdkconfig.m5stick-s3:1459` selects `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` with
`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` at line 1465. So log output
already reaches the USB-Serial/JTAG port as the secondary console. `pio device
monitor` on the USB port works today with no sdkconfig change. Verify this first.
Only if it does not work, document switching to
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` as a local, uncommitted change.

### ESP32 boards

`m5stick-c`, `m5stick-c-plus`, `m5stack-core` and `m5stack-core2` are plain
ESP32. `sdkconfig.m5stick-c-plus:357` has
`CONFIG_ESP_ROM_USB_SERIAL_DEVICE_NUM=-1`, that is, no USB-Serial/JTAG
peripheral. They reach the host through a USB-UART bridge. Their debug variants
get `build_type = debug` and verbose logging only. No `debug_tool`. State this
plainly in the README so nobody expects breakpoints on a StickC.

### Size

`build_type = debug` grows the binary. The project uses
`partitions_singleapp_large.csv` (`platformio.ini:11`), so there is headroom, but
check the reported flash usage on the smallest target, `m5stick-c`.

## Dependencies

None. This is the first PR and it unblocks on-device verification for every later
PR.

## Risks

- Debug builds may not fit or may behave differently under timing sensitive BLE
  code. Mitigation: the debug variants are additive and never used for release.
- Attaching a debugger halts the CPU, which drops BLE links and trips the task
  watchdog (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`, `sdkconfig.m5stick-s3:1476`).
  Document that breakpoints during an active camera connection will disconnect
  it. `CONFIG_ESP_TASK_WDT_PANIC` is not set, so the watchdog logs rather than
  reboots.
- A future contributor may copy a debug variant into CI. Name them clearly and
  say in the README that CI builds the five release environments only.

## Verification

Build matrix, all five release environments must still build unchanged:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Then build the five debug variants:

```
pio run -e m5stick-c-debug -e m5stick-c-plus-debug -e m5stack-core-debug \
        -e m5stack-core2-debug -e m5stick-s3-debug
```

On device, M5StickS3 over USB:

1. `pio run -e m5stick-s3-debug -t upload`
2. `pio device monitor -e m5stick-s3-debug`. Confirm the banner and the
   `furble version` line from `src/main.cpp:25` appear.
3. `pio debug -e m5stick-s3-debug`. Confirm the debugger attaches and halts at
   `app_main`.
4. Step once, continue, confirm the UI still runs.
5. Set a breakpoint in `UI::handleShutter` (`src/FurbleUI.cpp:540`), connect to
   the Fujifilm camera, press the shutter, confirm the breakpoint hits. Expect
   the BLE link to drop while halted. Record that behaviour in the PR body.

Regression check: flash the release `m5stick-s3` environment afterwards and
confirm behaviour is identical to master, including a fresh NVS boot.

Battery drain: not applicable. This PR changes no runtime behaviour in release
builds. Do not measure.

Camera coverage: Fujifilm only, and only as a smoke test that the debug build
still connects. No BLE code changes here.

## S3 post-flash auto-boot (watchdog_reset)

Symptom on macOS: a PlatformIO flash of the M5StickS3 completes to 100%, then
the post-write reset prints an errno-6 line and the device parks in the ROM
download stub. The screen stays black with the green LED lit and the app never
starts. A power cycle was the only way out.

Root cause: the StickS3 talks to the host over the built-in USB-Serial/JTAG
peripheral, not a USB-UART bridge. esptool's default `hard_reset` over
USB-Serial/JTAG only issues a core reset, which does not re-sample the boot
straps. The chip therefore stays in download mode after the write.

Fix: set esptool's reset mode to `watchdog_reset` for the S3 only. A watchdog
reset is a full chip reset that re-samples the straps, so the app boots. This
mode was added in esptool 4.9.0, which is the version PlatformIO already
bundles. PlatformIO passes `board_upload.after_reset` straight to esptool's
`--after`, so the fix is a single line in `platformio.ini`:

```
[env:m5stick-s3]
board_upload.after_reset = watchdog_reset
```

Scope: the S3 release env only. `m5stick-s3-debug` extends it and inherits the
setting. The four UART-bridge boards (`m5stick-c`, `m5stick-c-plus`,
`m5stack-core`, `m5stack-core2`) reset correctly with the default `hard_reset`
and stay unchanged. Do not put this in the shared `[env]` block.

Residual: a cosmetic errno-6 line may still print at the end of the upload on
macOS, but the app now boots without a manual power cycle.

## References

- ESP-IDF, JTAG debugging with the built-in USB-Serial/JTAG, ESP32-S3, GPIO19 and
  GPIO20:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/jtag-debugging/configure-builtin-jtag.html
- ESP-IDF, USB Serial/JTAG Controller Console,
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/usb-serial-jtag-console.html
- ESP-IDF, Logging library, `esp_log_level_set`, `CONFIG_LOG_DEFAULT_LEVEL`,
  `CONFIG_LOG_MAXIMUM_LEVEL`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/log.html
- PlatformIO, debugging, `debug_tool` and `debug_init_break`:
  https://docs.platformio.org/en/latest/plus/debugging.html
- PlatformIO, `build_type`:
  https://docs.platformio.org/en/latest/projectconf/sections/env/options/build/build_type.html
- PlatformIO, board `esp32-s3-devkitc-1`, on-board debug probe and `esp-builtin`:
  https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html
- PlatformIO, ESP-IDF framework and sdkconfig handling:
  https://docs.platformio.org/en/latest/frameworks/espidf.html
- PlatformIO, device monitor:
  https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
